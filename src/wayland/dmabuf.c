#include "dmabuf.h"
#include "log.h"
#include "render/renderer.h"
#include "wayland/globals.h"
#include <assert.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xf86drm.h>

/**
 * Open a DRM node for allocating buffers on.
 * The device is the one advertised by the compositor (which has to be able to
 * import the buffer to capture into it); its node type is not specified (and
 * its dev_t is not necessarily the one of the node we want), so the device is
 * resolved first and its render node is used (falling back to the primary
 * node for devices without a render node).
 */
static int open_drm_node(dev_t device) {
    drmDevicePtr dev = NULL;
    assert(device != 0);
    if (drmGetDeviceFromDevId(device, 0, &dev) != 0) {
        log_debug(
            "dmabuf: no DRM device for dev_t %lu\n", (unsigned long)device
        );
        return -1;
    }

    int fd = -1;
    if (dev->available_nodes & (1 << DRM_NODE_RENDER)) {
        fd = open(dev->nodes[DRM_NODE_RENDER], O_RDWR | O_CLOEXEC);
    }
    if (fd < 0 && (dev->available_nodes & (1 << DRM_NODE_PRIMARY))) {
        fd = open(dev->nodes[DRM_NODE_PRIMARY], O_RDWR | O_CLOEXEC);
    }
    if (fd < 0) {
        log_debug("dmabuf: couldn't open a DRM node\n");
    }
    drmFreeDevice(&dev);
    return fd;
}

/**
 * Create a gbm_bo with one of the given modifiers.
 * Modifiers are tried one at a time (in preference order): gbm otherwise gets
 * to pick any of them at once. Linear is preferred, since it can be imported
 * anywhere.
 */
static struct gbm_bo *create_bo(
    struct gbm_device *gbm,
    uint32_t width,
    uint32_t height,
    uint32_t format,
    const uint64_t *modifiers,
    size_t modifier_count
) {
    for (size_t i = 0; i < modifier_count; i++) {
        if (modifiers[i] == DRM_FORMAT_MOD_LINEAR) {
            struct gbm_bo *bo = gbm_bo_create_with_modifiers(
                gbm, width, height, format, &modifiers[i], 1
            );
            if (bo) {
                return bo;
            }
        }
    }

    for (size_t i = 0; i < modifier_count; i++) {
        // implicit modifiers are only negotiated by legacy protocols
        // (wlr-screencopy); ext capture always uses explicit modifiers
        if (modifiers[i] == DRM_FORMAT_MOD_INVALID ||
            modifiers[i] == DRM_FORMAT_MOD_LINEAR) {
            continue;
        }
        struct gbm_bo *bo = gbm_bo_create_with_modifiers(
            gbm, width, height, format, &modifiers[i], 1
        );
        if (bo) {
            return bo;
        }
    }
    return NULL;
}

static void params_handle_created(
    void *data,
    struct zwp_linux_buffer_params_v1 *params,
    struct wl_buffer *wl_buffer
) {
    DmabufBuffer *buffer = data;
    zwp_linux_buffer_params_v1_destroy(params);
    buffer->params = NULL;
    buffer->wl_buffer = wl_buffer;
    buffer->on_finished(
        buffer->texture,
        wl_buffer,
        buffer->width,
        buffer->height,
        buffer->format,
        buffer->user_data
    );
}

static void params_handle_failed(
    void *data, struct zwp_linux_buffer_params_v1 * /* params */
) {
    DmabufBuffer *buffer = data;
    buffer->on_finished(
        NULL, NULL, 0, 0, DRM_FORMAT_INVALID, buffer->user_data
    );
    // In this case the texture has been created, but isn't given to the user,
    // so we need to destroy it.
    // It has, however, taken ownership of the buffer struct.
    renderer_get_default()->texture_destroy(buffer->texture);
}

static const struct zwp_linux_buffer_params_v1_listener params_listener = {
    .created = params_handle_created,
    .failed = params_handle_failed,
};

void dmabuf_buffer_new(
    uint32_t width,
    uint32_t height,
    uint32_t format,
    const uint64_t *modifiers,
    size_t modifier_count,
    dev_t device,
    DmabufBufferCallback on_finished,
    void *data
) {
    if (wayland_globals.linux_dmabuf == NULL) {
        log_debug("dmabuf: zwp_linux_dmabuf_v1 not available\n");
        on_finished(NULL, NULL, 0, 0, DRM_FORMAT_INVALID, data);
        return;
    }

    int drm_fd = open_drm_node(device);
    if (drm_fd < 0) {
        on_finished(NULL, NULL, 0, 0, DRM_FORMAT_INVALID, data);
        return;
    }

    struct gbm_device *gbm = gbm_create_device(drm_fd);
    if (!gbm) {
        log_debug("dmabuf: gbm_create_device failed\n");
        close(drm_fd);
        on_finished(NULL, NULL, 0, 0, DRM_FORMAT_INVALID, data);
        return;
    }

    struct gbm_bo *bo =
        create_bo(gbm, width, height, format, modifiers, modifier_count);
    if (!bo) {
        log_debug("dmabuf: buffer allocation failed\n");
        gbm_device_destroy(gbm);
        close(drm_fd);
        on_finished(NULL, NULL, 0, 0, DRM_FORMAT_INVALID, data);
        return;
    }

    uint32_t plane_count = gbm_bo_get_plane_count(bo);
    if (plane_count == 0 || plane_count > DMABUF_MAX_PLANES) {
        log_debug("dmabuf: unsupported plane count %u\n", plane_count);
        gbm_bo_destroy(bo);
        gbm_device_destroy(gbm);
        close(drm_fd);
        on_finished(NULL, NULL, 0, 0, DRM_FORMAT_INVALID, data);
        return;
    }

    DmabufBuffer *buffer = calloc(1, sizeof(DmabufBuffer));
    buffer->width = width;
    buffer->height = height;
    buffer->format = format;
    buffer->modifier = gbm_bo_get_modifier(bo);
    buffer->plane_count = plane_count;
    buffer->bo = bo;
    buffer->gbm = gbm;
    buffer->drm_fd = drm_fd;
    buffer->on_finished = on_finished;
    buffer->user_data = data;
    for (uint32_t i = 0; i < plane_count; i++) {
        buffer->planes[i].fd = gbm_bo_get_fd_for_plane(bo, i);
        buffer->planes[i].offset = gbm_bo_get_offset(bo, i);
        buffer->planes[i].pitch = gbm_bo_get_stride_for_plane(bo, i);
        if (buffer->planes[i].fd < 0) {
            log_debug("dmabuf: gbm_bo_get_fd_for_plane failed\n");
            buffer->plane_count = i;
            dmabuf_buffer_destroy(buffer);
            on_finished(NULL, NULL, 0, 0, DRM_FORMAT_INVALID, data);
            return;
        }
        // gbm doesn't promise close-on-exec
        fcntl(buffer->planes[i].fd, F_SETFD, FD_CLOEXEC);
    }

    log_debug(
        "dmabuf: allocated %ux%u format %x, modifier %lx, %u plane(s), "
        "pitch %u\n",
        width,
        height,
        format,
        (unsigned long)buffer->modifier,
        buffer->plane_count,
        buffer->planes[0].pitch
    );

    // Import before attaching and capturing: a failure here can fall
    // back to shared memory without wasting a compositor copy.
    // The buffer must also be attached before the compositor receives it via
    // linux_dmabuf to satisfy its requirements.
    buffer->texture = renderer_get_default()->texture_new_from_dmabuf(buffer);
    if (!buffer->texture) {
        log_debug("dmabuf: renderer import failed\n");
        dmabuf_buffer_destroy(buffer);
        on_finished(NULL, NULL, 0, 0, DRM_FORMAT_INVALID, data);
        return;
    }

    struct zwp_linux_buffer_params_v1 *params =
        zwp_linux_dmabuf_v1_create_params(wayland_globals.linux_dmabuf);
    for (uint32_t i = 0; i < buffer->plane_count; i++) {
        zwp_linux_buffer_params_v1_add(
            params,
            buffer->planes[i].fd,
            i,
            buffer->planes[i].offset,
            buffer->planes[i].pitch,
            buffer->modifier >> 32,
            buffer->modifier & 0xffffffff
        );
    }
    buffer->params = params;
    zwp_linux_buffer_params_v1_add_listener(params, &params_listener, buffer);
    // Use async creation to tolerate compositor-side import errors
    zwp_linux_buffer_params_v1_create(params, width, height, format, 0);
}

void dmabuf_buffer_destroy(DmabufBuffer *buffer) {
    // destroying the params object drops the pending creation, if any
    // (and with it any further events about it)
    if (buffer->params) {
        zwp_linux_buffer_params_v1_destroy(buffer->params);
    }
    if (buffer->wl_buffer) {
        wl_buffer_destroy(buffer->wl_buffer);
    }
    for (uint32_t i = 0; i < buffer->plane_count; i++) {
        if (buffer->planes[i].fd >= 0) {
            close(buffer->planes[i].fd);
        }
    }
    gbm_bo_destroy(buffer->bo);
    gbm_device_destroy(buffer->gbm);
    close(buffer->drm_fd);
    free(buffer);
}
