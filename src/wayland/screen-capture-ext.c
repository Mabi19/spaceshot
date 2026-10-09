#include "image.h"
#include "log.h"
#include "render/renderer.h"
#include "screen-capture.h"
#include "wayland/dmabuf.h"
#include "wayland/globals.h"
#include "wayland/shared-memory.h"
#include <assert.h>
#include <drm_fourcc.h>
#include <ext-image-capture-source-client.h>
#include <ext-image-copy-capture-client.h>
#include <string.h>
#include <sys/types.h>
#include <wayland-client.h>

// This struct is refcounted, and references are logically owned by:
// - the session (while session is set)
// - the frame (from its creation until either ready or failed)
// - the dmabuf import process (while it is active)
typedef struct {
    // Whether to capture into a dmabuf (when possible)
    bool use_dmabuf;
    // Whether a capture has been started.
    // This is used to ignore any further done events on the session.
    bool capture_started;
    // general frame data
    uint32_t width;
    uint32_t height;
    // Transform applied by the compositor to the captured buffer
    ImageTransform transform;
    // shm negotiation
    enum wl_shm_format selected_format;
    bool has_selected_format;
    SharedBuffer *shm_buffer;
    Image *shm_result;
    // dmabuf negotiation
    dev_t dmabuf_device;
    uint32_t selected_dmabuf_format;
    uint64_t *dmabuf_modifiers;
    size_t dmabuf_modifier_count;
    bool has_dmabuf_device;
    bool has_selected_dmabuf_format;
    RenderTexture *dmabuf_texture;
    // Whether the dmabuf texture has been filled in by the compositor
    // and is safe to use.
    bool dmabuf_ready;
    // Borrowed from dmabuf_texture.
    struct wl_buffer *dmabuf_wl_buffer;

    // associated Wayland objects
    struct ext_image_capture_source_v1 *source;
    struct ext_image_copy_capture_session_v1 *session;
    FrameCaptureCallback frame_callback;
    WrappedOutput *output;
    WrappedToplevel *toplevel;
    void *user_data;
    // this context is passed as user data to several listeners
    // so refcounting is necessary
    int ref_count;
} FrameContext;

static void frame_context_unref(FrameContext *context) {
    assert(context->ref_count > 0);
    context->ref_count--;
    if (context->ref_count == 0) {
        CaptureFrame *frame = NULL;
        if (context->shm_result || context->dmabuf_ready) {
            frame = calloc(1, sizeof(CaptureFrame));
            frame->image = context->shm_result;
            frame->texture = context->dmabuf_texture;
            if (context->dmabuf_ready) {
                // dmabuf captures don't have CPU-side data;
                // the dimensions may be swapped by the buffer transform.
                ImageTransform correction =
                    image_transform_invert(context->transform);
                frame->width = image_transform_swaps_dimensions(correction)
                                   ? context->height
                                   : context->width;
                frame->height = image_transform_swaps_dimensions(correction)
                                    ? context->width
                                    : context->height;
                frame->compatible_format =
                    image_format_from_drm(context->selected_dmabuf_format);
            } else {
                frame->width = frame->image->width;
                frame->height = frame->image->height;
                frame->compatible_format = context->shm_result->format;
            }
            if (context->output) {
                frame->type = CAPTURE_FRAME_TYPE_OUTPUT;
                frame->output = context->output;
            } else {
                frame->type = CAPTURE_FRAME_TYPE_TOPLEVEL;
                frame->toplevel = context->toplevel;
            }
            wl_list_init(&frame->link);
        }
        context->frame_callback(frame, context->user_data);

        if (context->shm_buffer) {
            shared_buffer_destroy(context->shm_buffer);
        }
        free(context->dmabuf_modifiers);
        if (context->source) {
            ext_image_capture_source_v1_destroy(context->source);
        }
        if (context->session) {
            ext_image_copy_capture_session_v1_destroy(context->session);
        }
        free(context);
    }
}

static void
start_frame_capture(FrameContext *context, struct wl_buffer *wl_buffer);
static bool start_shm_capture(FrameContext *context);

// Frame event handlers

static void frame_handle_damage(
    void * /* data */,
    struct ext_image_copy_capture_frame_v1 * /* frame */,
    int32_t /* x */,
    int32_t /* y */,
    int32_t /* width */,
    int32_t /* height */
) {
    // we're only capturing one frame, so this does not matter
}

static void frame_handle_failed(
    void *data, struct ext_image_copy_capture_frame_v1 *frame, uint32_t reason
) {
    FrameContext *context = data;
    ext_image_copy_capture_frame_v1_destroy(frame);

    if (context->dmabuf_texture != NULL) {
        log_debug("dmabuf capture failed (%u)\n", reason);
        renderer_get_default()->texture_destroy(context->dmabuf_texture);
        context->dmabuf_texture = NULL;

        // Retry once with shared memory: the compositor may be able to copy
        // into a shm buffer even when it can't render into the dmabuf.
        // The dmabuf_texture being NULL next time means this will not run again
        if (context->session && start_shm_capture(context)) {
            log_debug("... and retried with shm\n");
            frame_context_unref(context);
            return;
        }
    }

    // Both dmabuf and shm capture failed. Give up.
    if (context->session) {
        ext_image_copy_capture_session_v1_destroy(context->session);
        context->session = NULL;
        frame_context_unref(context);
    }
    frame_context_unref(context);
}

static void frame_handle_presentation_time(
    void * /* data */,
    struct ext_image_copy_capture_frame_v1 * /* frame */,
    uint32_t /* tv_sec_hi */,
    uint32_t /* tv_sec_lo */,
    uint32_t /* tv_nsec */
) {
    // we don't need this
}

static void
frame_handle_ready(void *data, struct ext_image_copy_capture_frame_v1 *frame) {
    FrameContext *context = data;

    if (context->dmabuf_texture) {
        renderer_get_default()->texture_dmabuf_set_transform(
            context->dmabuf_texture, context->transform
        );
        context->dmabuf_ready = true;
    } else {
        uint32_t stride = image_format_default_stride(
            image_format_from_wl(context->selected_format), context->width
        );
        Image *captured = image_new_from_wayland(
            context->selected_format,
            context->shm_buffer->data,
            context->width,
            context->height,
            stride
        );
        if (context->transform == IMAGE_TRANSFORM_NORMAL) {
            context->shm_result = captured;
        } else {
            context->shm_result = image_transform(
                captured, image_transform_invert(context->transform)
            );
            image_destroy(captured);
        }
    }

    // This deletes BOTH objects which have a reference to the frame context
    // (the session's, if it's still held, and the frame's).
    // So, unref accordingly!
    if (context->session) {
        ext_image_copy_capture_session_v1_destroy(context->session);
        context->session = NULL;
        frame_context_unref(context);
    }
    ext_image_copy_capture_frame_v1_destroy(frame);
    frame_context_unref(context);
}

static void frame_handle_transform(
    void *data,
    struct ext_image_copy_capture_frame_v1 * /* frame */,
    uint32_t transform
) {
    FrameContext *context = data;
    context->transform = image_transform_from_wl(transform);
    log_debug("got buffer transform %u\n", transform);
}

static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
    .damage = frame_handle_damage,
    .failed = frame_handle_failed,
    .presentation_time = frame_handle_presentation_time,
    .ready = frame_handle_ready,
    .transform = frame_handle_transform,
};

// Session event handlers

static void session_handle_buffer_size(
    void *data,
    struct ext_image_copy_capture_session_v1 * /* session */,
    uint32_t width,
    uint32_t height
) {
    FrameContext *context = data;
    if (!context->capture_started) {
        context->width = width;
        context->height = height;
    }
}

static void session_handle_dmabuf_device(
    void *data,
    struct ext_image_copy_capture_session_v1 * /* session */,
    struct wl_array *device
) {
    FrameContext *context = data;

    if (!context->use_dmabuf) {
        return;
    }

    // the array contains a single dev_t value
    // (wl_arrays are essentially just bytestrings)
    if (device->size == sizeof(dev_t)) {
        memcpy(&context->dmabuf_device, device->data, sizeof(dev_t));
        context->has_dmabuf_device = true;
        log_debug(
            "got dmabuf device %lu\n", (unsigned long)context->dmabuf_device
        );
    } else {
        report_warning("dmabuf device isn't dev_t sized");
    }
}

/**
 * Because the formats overlap so much,
 * we can reuse one function for both wl_shm and DRM formats.
 */
static inline int format_priority(uint32_t format) {
    // Use 10-bit if available, otherwise fall back to 8-bit.
    // Alpha is preferred over no alpha
    // because toplevel captures may be transparent.
    // All wl_shm formats are defined as the same constants at the equivalent
    // DRM formats, except for ARGB8888 (=0) and XRGB8888 (=1).
    switch (format) {
    case DRM_FORMAT_XRGB2101010:
    case DRM_FORMAT_XBGR2101010:
        return 3;
    case WL_SHM_FORMAT_ARGB8888:
    case DRM_FORMAT_ARGB8888:
        return 2;
    case WL_SHM_FORMAT_XRGB8888:
    case DRM_FORMAT_XRGB8888:
    case DRM_FORMAT_XBGR8888:
        return 1;
    default:
        return -1;
    }
}

static void session_handle_dmabuf_format(
    void *data,
    struct ext_image_copy_capture_session_v1 * /* session */,
    uint32_t format,
    struct wl_array *modifiers
) {
    FrameContext *context = data;

    if (!context->use_dmabuf) {
        return;
    }

    log_debug("got dmabuf format %x\n", format);

    int priority = format_priority(format);
    if (priority >= 0) {
        if (!context->has_selected_dmabuf_format ||
            format_priority(context->selected_dmabuf_format) < priority) {
            // The dmabuf format is also ingested by EGL, we need to ensure
            // it is compatible with whatever the compositor gives us
            size_t count = modifiers->size / sizeof(uint64_t);
            uint64_t *list = NULL;
            if (count > 0) {
                list = malloc(count * sizeof(uint64_t));
                memcpy(list, modifiers->data, count * sizeof(uint64_t));
            }
            // only keep modifiers which the renderer can import as a texture
            count = renderer_get_default()->intersect_dmabuf_modifiers(
                format, list, count
            );
            if (count == 0) {
                log_debug("no texture-importable modifiers, skipping\n");
                free(list);
                return;
            }
            for (size_t i = 0; i < count; i++) {
                log_debug("  modifier %zu: %lx\n", i, (unsigned long)list[i]);
            }
            free(context->dmabuf_modifiers);
            context->dmabuf_modifiers = list;
            context->dmabuf_modifier_count = count;
            context->selected_dmabuf_format = format;
            context->has_selected_dmabuf_format = true;
        }
    }
    log_debug("skipping\n");
}

static void session_handle_shm_format(
    void *data,
    struct ext_image_copy_capture_session_v1 * /* session */,
    uint32_t format
) {
    FrameContext *context = data;

    log_debug("got buffer format %x\n", format);
    int priority = format_priority(format);
    if (priority >= 0) {
        if (!context->has_selected_format ||
            format_priority(context->selected_format) < priority) {
            context->selected_format = format;
            context->has_selected_format = true;
        }
    }
    log_debug("skipping\n");
}

static void dmabuf_handle_finish(
    RenderTexture *texture, struct wl_buffer *wl_buffer, void *data
) {
    FrameContext *context = data;

    if (!context->session) {
        // the session stopped while the buffer was being created
        if (texture) {
            renderer_get_default()->texture_destroy(texture);
        }
        frame_context_unref(context);
        return;
    }

    if (texture) {
        assert(!context->dmabuf_texture && !context->dmabuf_wl_buffer);
        context->dmabuf_texture = texture;
        context->dmabuf_wl_buffer = wl_buffer;
        start_frame_capture(context, wl_buffer);
        frame_context_unref(context);
        return;
    } else {
        log_debug("dmabuf buffer creation failed\n");
        if (start_shm_capture(context)) {
            log_debug("... and retried with shm\n");
            frame_context_unref(context);
            return;
        } else {
            // give up
            ext_image_copy_capture_session_v1_destroy(context->session);
            context->session = NULL;
            // session unref
            frame_context_unref(context);
            // dmabuf unref
            frame_context_unref(context);
        }
    }
}

static void session_handle_done(
    void *data, struct ext_image_copy_capture_session_v1 *session
) {
    FrameContext *context = data;

    if (context->capture_started) {
        log_debug("ignoring resent session constraints\n");
        return;
    }
    context->capture_started = true;

    if (context->use_dmabuf && context->has_selected_dmabuf_format &&
        context->has_dmabuf_device) {
        // hold a reference for the asynchronous dmabuf creation
        context->ref_count++;
        dmabuf_buffer_new(
            context->width,
            context->height,
            context->selected_dmabuf_format,
            context->dmabuf_modifiers,
            context->dmabuf_modifier_count,
            context->dmabuf_device,
            dmabuf_handle_finish,
            context
        );
    } else if (!start_shm_capture(context)) {
        log_debug("starting capture after session done failed\n");
        ext_image_copy_capture_session_v1_destroy(session);
        context->session = NULL;
        frame_context_unref(context);
    }
}

static void session_handle_stopped(
    void *data, struct ext_image_copy_capture_session_v1 *session
) {
    FrameContext *context = data;
    // The session is unusable now; destroy it and drop its reference.
    // (While context->session is set, the session's reference is held.)
    ext_image_copy_capture_session_v1_destroy(session);
    context->session = NULL;
    frame_context_unref(context);
    // We assume that any outstanding frame objects will either finish or fail
    // and be cleaned up that way.
}

// Session helpers

static void
start_frame_capture(FrameContext *context, struct wl_buffer *wl_buffer) {
    struct ext_image_copy_capture_frame_v1 *frame =
        ext_image_copy_capture_session_v1_create_frame(context->session);
    context->ref_count++;
    ext_image_copy_capture_frame_v1_add_listener(
        frame, &frame_listener, context
    );
    ext_image_copy_capture_frame_v1_attach_buffer(frame, wl_buffer);
    ext_image_copy_capture_frame_v1_damage_buffer(
        frame, 0, 0, context->width, context->height
    );
    ext_image_copy_capture_frame_v1_capture(frame);
}

static bool start_shm_capture(FrameContext *context) {
    if (context->width == 0 || context->height == 0 ||
        !context->has_selected_format) {
        return false;
    }
    assert(!context->shm_buffer);

    uint32_t stride = image_format_default_stride(
        image_format_from_wl(context->selected_format), context->width
    );
    context->shm_buffer = shared_buffer_new(
        context->width, context->height, stride, context->selected_format
    );
    if (!context->shm_buffer) {
        return false;
    }
    start_frame_capture(context, context->shm_buffer->wl_buffer);
    return true;
}

static const struct ext_image_copy_capture_session_v1_listener
    session_listener = {
        .buffer_size = session_handle_buffer_size,
        .dmabuf_device = session_handle_dmabuf_device,
        .dmabuf_format = session_handle_dmabuf_format,
        .shm_format = session_handle_shm_format,
        .done = session_handle_done,
        .stopped = session_handle_stopped,
};

void capture_output_ext(
    WrappedOutput *output,
    bool use_dmabuf,
    FrameCaptureCallback image_callback,
    void *data
) {
    FrameContext *context = calloc(1, sizeof(FrameContext));
    context->use_dmabuf = use_dmabuf;
    context->frame_callback = image_callback;
    context->output = output;
    context->user_data = data;

    context->source = ext_output_image_capture_source_manager_v1_create_source(
        wayland_globals.ext_output_capture_source_manager, output->wl_output
    );

    context->session = ext_image_copy_capture_manager_v1_create_session(
        wayland_globals.ext_image_copy_capture_manager, context->source, 0
    );

    context->ref_count = 1;
    ext_image_copy_capture_session_v1_add_listener(
        context->session, &session_listener, context
    );
}

void capture_toplevel_ext(
    WrappedToplevel *toplevel,
    bool use_dmabuf,
    FrameCaptureCallback image_callback,
    void *data
) {
    FrameContext *context = calloc(1, sizeof(FrameContext));
    context->use_dmabuf = use_dmabuf;
    context->frame_callback = image_callback;
    context->toplevel = toplevel;
    context->user_data = data;

    context->source =
        ext_foreign_toplevel_image_capture_source_manager_v1_create_source(
            wayland_globals.ext_toplevel_capture_source_manager,
            toplevel->handle
        );

    context->session = ext_image_copy_capture_manager_v1_create_session(
        wayland_globals.ext_image_copy_capture_manager, context->source, 0
    );

    context->ref_count = 1;
    ext_image_copy_capture_session_v1_add_listener(
        context->session, &session_listener, context
    );
}

bool capture_output_ext_is_available() {
    return wayland_globals.ext_output_capture_source_manager != NULL &&
           wayland_globals.ext_image_copy_capture_manager != NULL;
}

bool capture_toplevel_ext_is_available() {
    return wayland_globals.ext_toplevel_capture_source_manager != NULL &&
           wayland_globals.ext_image_copy_capture_manager != NULL;
}
