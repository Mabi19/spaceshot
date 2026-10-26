#include "alloc.h"
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
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

constexpr int MAX_RETRIES = 3;

typedef enum {
    // A capture hasn't started.
    CAPTURE_METHOD_NONE,
    // A capture has started using shared memory.
    CAPTURE_METHOD_SHM,
    // A capture has started using a dmabuf.
    CAPTURE_METHOD_DMABUF,
} CaptureMethod;

typedef struct {
    SharedBuffer *buffer;
    Image *result;
} ShmFrameData;

typedef struct {
    RenderTexture *texture;
    // RenderTextures are opaque and don't expose their size or format.
    // We need to store them separately.
    uint32_t width;
    uint32_t height;
    uint32_t format;

    // Whether the texture's dmabuf has been filled in by the compositor
    // and is safe to use for drawing/downloading.
    bool ready;
} DmabufFrameData;

// The parameters of the frame being captured.
typedef struct {
    union {
        ShmFrameData shm;
        DmabufFrameData dmabuf;
    };

    // shm transforms the image, and
    // dmabufs apply it to the texture once finished.
    ImageTransform transform;
    CaptureMethod method;
} FrameData;

// This struct is refcounted, and references are logically owned by:
// - the session (while session is set)
// - the frame (from its creation until either ready or failed)
// - the retry sync callback (from the wl_display_sync until done)
// - the dmabuf import process (while it is active)
typedef struct {
    // Whether to capture into a dmabuf (when possible)
    bool use_dmabuf;
    // Whether a capture has been started.
    // This is used to ignore any further done events on the session.
    bool capture_started;
    int retries;

    // These are sent by the session
    uint32_t pending_width;
    uint32_t pending_height;
    uint32_t width;
    uint32_t height;

    // shm session negotiation
    enum wl_shm_format pending_shm_format;
    enum wl_shm_format shm_format;
    bool has_pending_shm_format;
    bool has_shm_format;

    // dmabuf session negotiation
    dev_t pending_dmabuf_device;
    dev_t dmabuf_device;
    uint32_t pending_dmabuf_format;
    uint32_t dmabuf_format;
    bool has_pending_dmabuf_device;
    bool has_dmabuf_device;
    bool has_pending_dmabuf_format;
    bool has_dmabuf_format;
    uint64_t *pending_dmabuf_modifiers;
    size_t pending_dmabuf_modifier_count;
    uint64_t *dmabuf_modifiers;
    size_t dmabuf_modifier_count;

    // data to make the CaptureFrame struct
    FrameData frame;

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

static void try_start_capture(FrameContext *context);

static void frame_context_unref(FrameContext *context) {
    assert(context->ref_count > 0);
    context->ref_count--;
    if (context->ref_count == 0) {
        CaptureFrame *result = NULL;
        FrameData *frame = &context->frame;
        switch (frame->method) {
        case CAPTURE_METHOD_DMABUF: {
            if (frame->dmabuf.ready) {
                result = ccalloc(1, sizeof(CaptureFrame));
                result->texture = frame->dmabuf.texture;
                result->width = frame->dmabuf.width;
                result->height = frame->dmabuf.height;
                result->compatible_format =
                    image_format_from_drm(frame->dmabuf.format);
            } else {
                // should be cleaned up when failed
                assert(!frame->dmabuf.texture);
            }
            break;
        }
        case CAPTURE_METHOD_SHM: {
            if (frame->shm.result) {
                result = ccalloc(1, sizeof(CaptureFrame));
                result->image = frame->shm.result;
                result->width = frame->shm.result->width;
                result->height = frame->shm.result->height;
                result->compatible_format = frame->shm.result->format;
            } else {
                // should be cleaned up when failed or succeeded
                assert(!frame->shm.buffer);
            }
            break;
        }
        case CAPTURE_METHOD_NONE:
            break;
        default:
            REPORT_UNHANDLED("capture method", "%d", frame->method);
        }
        if (result) {
            if (context->output) {
                result->type = CAPTURE_FRAME_TYPE_OUTPUT;
                result->output = context->output;
            } else {
                result->type = CAPTURE_FRAME_TYPE_TOPLEVEL;
                result->toplevel = context->toplevel;
            }
            wl_list_init(&result->link);
        }
        context->frame_callback(result, context->user_data);

        free(context->pending_dmabuf_modifiers);
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

static void retry_constraints_sync(
    void *data, struct wl_callback *callback, uint32_t /* callback_data */
) {
    FrameContext *context = data;
    wl_callback_destroy(callback);
    if (context->session) {
        try_start_capture(context);
    }
    frame_context_unref(context);
}

static const struct wl_callback_listener retry_constraints_listener = {
    .done = retry_constraints_sync,
};

static void frame_handle_failed(
    void *data,
    struct ext_image_copy_capture_frame_v1 *ext_frame,
    uint32_t reason
) {
    FrameContext *context = data;
    FrameData *frame = &context->frame;
    ext_image_copy_capture_frame_v1_destroy(ext_frame);

    CaptureMethod method = frame->method;
    switch (method) {
    case CAPTURE_METHOD_DMABUF:
        log_debug("dmabuf capture failed (%u)\n", reason);
        renderer_get_default()->texture_destroy(frame->dmabuf.texture);
        frame->dmabuf.texture = NULL;
        break;
    case CAPTURE_METHOD_SHM:
        log_debug("shm capture failed (%u)\n", reason);
        shared_buffer_destroy(frame->shm.buffer);
        frame->shm.buffer = NULL;
        break;
    default:
        REPORT_UNHANDLED("capture method", "%d", method);
    }
    frame->method = CAPTURE_METHOD_NONE;

    if (context->session) {
        if (context->retries < MAX_RETRIES &&
            reason != EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_STOPPED) {
            if (
                reason ==
                EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS
            ) {
                context->retries++;
                log_debug("retrying due to changed buffer constraints\n");
                // The compositor should've/is about to send some new
                // constraints if it is complaining about this. Doing a
                // wl_display.sync() should ensure anything sent directly after
                // this event will make it through.
                context->ref_count++;
                struct wl_callback *callback =
                    wl_display_sync(wayland_globals.display);
                wl_callback_add_listener(
                    callback, &retry_constraints_listener, context
                );

                // frame ref
                frame_context_unref(context);
                return;
            } else if (method == CAPTURE_METHOD_DMABUF) {
                // Something went wrong with copying the data.
                // The compositor may have a better time copying over shm
                context->retries++;
                context->use_dmabuf = false;
                log_debug("retrying with shm due to failed dmabuf copy\n");
                try_start_capture(context);
                // frame ref
                frame_context_unref(context);
                return;
            }
        }

        // All the viable fallbacks failed. Give up.
        ext_image_copy_capture_session_v1_destroy(context->session);
        context->session = NULL;
        frame_context_unref(context);
    }
    // frame ref
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

static void frame_handle_ready(
    void *data, struct ext_image_copy_capture_frame_v1 *ext_frame
) {
    FrameContext *context = data;
    FrameData *frame = &context->frame;

    switch (frame->method) {
    case CAPTURE_METHOD_DMABUF: {
        renderer_get_default()->texture_dmabuf_set_transform(
            frame->dmabuf.texture, frame->transform
        );
        bool swap = image_transform_swaps_dimensions(frame->transform);
        uint32_t width = swap ? frame->dmabuf.height : frame->dmabuf.width;
        uint32_t height = swap ? frame->dmabuf.width : frame->dmabuf.height;
        frame->dmabuf.width = width;
        frame->dmabuf.height = height;

        frame->dmabuf.ready = true;
        break;
    }
    case CAPTURE_METHOD_SHM: {
        Image *captured = image_new_from_wayland(
            frame->shm.buffer->format,
            frame->shm.buffer->data,
            frame->shm.buffer->width,
            frame->shm.buffer->height,
            frame->shm.buffer->stride
        );
        if (frame->transform == IMAGE_TRANSFORM_NORMAL) {
            frame->shm.result = captured;
        } else {
            frame->shm.result = image_transform(
                captured, image_transform_invert(frame->transform)
            );
            image_destroy(captured);
        }
        shared_buffer_destroy(frame->shm.buffer);
        frame->shm.buffer = NULL;
        break;
    }
    default:
        REPORT_UNHANDLED("capture method", "%d", frame->method);
    }

    // This deletes BOTH objects which have a reference to the frame context
    // (the session's, if it's still held, and the frame's).
    // So, unref accordingly!
    if (context->session) {
        ext_image_copy_capture_session_v1_destroy(context->session);
        context->session = NULL;
        frame_context_unref(context);
    }
    ext_image_copy_capture_frame_v1_destroy(ext_frame);
    frame_context_unref(context);
}

static void frame_handle_transform(
    void *data,
    struct ext_image_copy_capture_frame_v1 * /* frame */,
    uint32_t transform
) {
    FrameData *frame = &((FrameContext *)data)->frame;
    frame->transform = image_transform_from_wl(transform);
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
    context->pending_width = width;
    context->pending_height = height;
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
        memcpy(&context->pending_dmabuf_device, device->data, sizeof(dev_t));
        context->has_pending_dmabuf_device = true;
        log_debug(
            "got dmabuf device %lu\n",
            (unsigned long)context->pending_dmabuf_device
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
        if (!context->has_pending_dmabuf_format ||
            format_priority(context->pending_dmabuf_format) < priority) {
            // The dmabuf format is also ingested by EGL, we need to ensure
            // it is compatible with whatever the compositor gives us
            size_t count = modifiers->size / sizeof(uint64_t);
            uint64_t *list = NULL;
            if (count > 0) {
                list = cmalloc(count * sizeof(uint64_t));
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
            free(context->pending_dmabuf_modifiers);
            context->pending_dmabuf_modifiers = list;
            context->pending_dmabuf_modifier_count = count;
            context->pending_dmabuf_format = format;
            context->has_pending_dmabuf_format = true;
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
        if (!context->has_pending_shm_format ||
            format_priority(context->pending_shm_format) < priority) {
            context->pending_shm_format = format;
            context->has_pending_shm_format = true;
        }
    }
    log_debug("skipping\n");
}

static void session_handle_done(
    void *data, struct ext_image_copy_capture_session_v1 * /* session */
) {
    FrameContext *context = data;

    // Pending formats are special: they're assembled over multiple events.
    // Because of this we need to reset the pending data every done().
    context->width = context->pending_width;
    context->height = context->pending_height;

    context->shm_format = context->pending_shm_format;
    context->has_shm_format = context->has_pending_shm_format;
    context->has_pending_shm_format = false;

    context->dmabuf_device = context->pending_dmabuf_device;
    context->has_dmabuf_device = context->has_pending_dmabuf_device;
    context->has_pending_dmabuf_device = false;

    context->dmabuf_format = context->pending_dmabuf_format;
    context->has_dmabuf_format = context->has_pending_dmabuf_format;
    context->has_pending_dmabuf_format = false;

    free(context->dmabuf_modifiers);
    context->dmabuf_modifiers = context->pending_dmabuf_modifiers;
    context->dmabuf_modifier_count = context->pending_dmabuf_modifier_count;
    context->pending_dmabuf_modifiers = NULL;
    context->pending_dmabuf_modifier_count = 0;

    if (context->capture_started) {
        log_debug("session constraints resent\n");
        return;
    }
    context->capture_started = true;
    try_start_capture(context);
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
    // Any outstanding frame objects should either finish or fail
    // and be cleaned up that way.
}

// Session helpers

static void start_frame_capture(
    FrameContext *context,
    struct wl_buffer *wl_buffer,
    uint32_t width,
    uint32_t height
) {
    struct ext_image_copy_capture_frame_v1 *frame =
        ext_image_copy_capture_session_v1_create_frame(context->session);
    context->ref_count++;
    ext_image_copy_capture_frame_v1_add_listener(
        frame, &frame_listener, context
    );
    ext_image_copy_capture_frame_v1_attach_buffer(frame, wl_buffer);
    ext_image_copy_capture_frame_v1_damage_buffer(frame, 0, 0, width, height);
    ext_image_copy_capture_frame_v1_capture(frame);
}

static void dmabuf_handle_finish(
    RenderTexture *texture,
    struct wl_buffer *wl_buffer,
    uint32_t width,
    uint32_t height,
    uint32_t drm_format,
    void *data
) {
    FrameContext *context = data;

    if (!context->session) {
        // the session stopped while the buffer was being created
        if (texture) {
            renderer_get_default()->texture_destroy(texture);
        }
        frame_context_unref(context);
        return;
    } else {
        // the session is alive, which means its ref is held,
        // so dropping the dmabuf ref won't cleanup now
        frame_context_unref(context);
    }

    if (texture) {
        assert(context->frame.method == CAPTURE_METHOD_NONE);
        context->frame = (FrameData){
            .method = CAPTURE_METHOD_DMABUF,
            .transform = IMAGE_TRANSFORM_NORMAL,
            .dmabuf = {
                .texture = texture,
                .width = width,
                .height = height,
                .format = drm_format,
                .ready = false,
            },
        };

        start_frame_capture(context, wl_buffer, width, height);
    } else {
        log_debug("dmabuf buffer creation failed, retrying shm-only\n");
        context->use_dmabuf = false;
        try_start_capture(context);
    }
}

static void try_start_capture(FrameContext *context) {
    assert(context->frame.method == CAPTURE_METHOD_NONE);
    if (context->width == 0 || context->height == 0) {
        log_debug("capture size wasn't sent\n");
        goto error;
    }

    if (context->use_dmabuf && context->has_dmabuf_format &&
        context->has_dmabuf_device) {
        // hold a reference for the asynchronous dmabuf creation
        context->ref_count++;
        dmabuf_buffer_new(
            context->width,
            context->height,
            context->dmabuf_format,
            context->dmabuf_modifiers,
            context->dmabuf_modifier_count,
            context->dmabuf_device,
            dmabuf_handle_finish,
            context
        );
        return;
    } else if (context->has_shm_format) {
        if (context->width > INT_MAX) {
            // this would overflow the stride calculation
            goto error;
        }
        uint32_t stride = image_format_default_stride(
            image_format_from_wl(context->shm_format), context->width
        );

        SharedBuffer *shm_buffer = shared_buffer_new(
            context->width, context->height, stride, context->shm_format
        );
        if (!shm_buffer) {
            goto error;
        }
        context->frame = (FrameData){
            .method = CAPTURE_METHOD_SHM,
            .transform = IMAGE_TRANSFORM_NORMAL,
            .shm = {
                .buffer = shm_buffer,
                .result = NULL,
            }
        };

        start_frame_capture(
            context,
            shm_buffer->wl_buffer,
            shm_buffer->width,
            shm_buffer->height
        );
        return;
    }

error:
    log_debug("starting capture failed\n");
    ext_image_copy_capture_session_v1_destroy(context->session);
    context->session = NULL;
    frame_context_unref(context);
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
    FrameContext *context = ccalloc(1, sizeof(FrameContext));
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
    FrameContext *context = ccalloc(1, sizeof(FrameContext));
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
