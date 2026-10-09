#pragma once
#include "render/texture.h"
#include <gbm.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <wayland-client.h>

constexpr size_t DMABUF_MAX_PLANES = 4;

// TODO: restructure this so that texture_new_from_dmabuf creates a
// RenderTexture directly. The RenderTexture needs to be created before passing
// the dmabuf to the compositor, and calling code doesn't actually care about
// the dmabuf itself, only the wl_buffer.
// So I guess it can actually return both.

/**
 * Called when dmabuf_buffer_new finishes.
 * The wl_buffer is logically borrowed from the texture.
 * Either both texture and wl_buffer will be present or they will both be NULL.
 */
typedef void (*DmabufBufferCallback)(
    RenderTexture *texture, struct wl_buffer *wl_buffer, void *data
);

/**
 * A dmabuf-backed wl_buffer allocation.
 * The buffer's contents are stored according to the specified DRM fourcc
 * format and modifier. This type is not exposed to calling code,
 * but renderers make use of it (and should own it upon texture_new_from_dmabuf)
 */
typedef struct {
    uint32_t width;
    uint32_t height;
    /** DRM fourcc code. */
    uint32_t format;
    /** DRM format modifier, shared by all planes. */
    uint64_t modifier;
    uint32_t plane_count;
    /** File descriptors, offsets and pitches (in bytes) of the planes. */
    struct {
        int fd;
        uint32_t offset;
        uint32_t pitch;
    } planes[DMABUF_MAX_PLANES];
    /**
     * The wl_buffer. This is NULL until the compositor has accepted the
     * buffer (see dmabuf_buffer_new).
     */
    struct wl_buffer *wl_buffer;
    /**
     * The allocation objects backing the buffer, kept until destruction so
     * that the buffer outlives both the exporting and the importing side
     * without having to reason about references.
     */
    struct gbm_bo *bo;
    struct gbm_device *gbm;
    int drm_fd;
    /** Pending wl_buffer creation, if any. */
    struct zwp_linux_buffer_params_v1 *params;
    DmabufBufferCallback on_finished;
    // When finished the texture actually owns the dmabuf and not the other way
    // around.
    RenderTexture *texture;
    void *user_data;
} DmabufBuffer;

/**
 * Allocate a dmabuf and wrap it in RenderTexture + wl_buffer pair
 * asynchronously.
 *
 * modifiers is the list of acceptable DRM format modifiers; linear is
 * preferred (it can be imported anywhere), the rest are tried in order.
 *
 * device is the DRM device (as a dev_t value) the buffer must be allocated on.
 * This is the device advertised by the compositor: it has to be able to
 * import the buffer to capture into it. (Importing the buffer as a texture
 * works across devices through PRIME, though possibly with a copy involved;
 * and some modifiers only import on their own device.)
 *
 * on_finished may be called immediately (before this function returns).
 */
void dmabuf_buffer_new(
    uint32_t width,
    uint32_t height,
    uint32_t format,
    const uint64_t *modifiers,
    size_t modifier_count,
    dev_t device,
    DmabufBufferCallback on_finished,
    void *data
);

void dmabuf_buffer_destroy(DmabufBuffer *buffer);
