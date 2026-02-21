#include "image.h"
#include "log.h"
#include "screen-capture.h"
#include "wayland/globals.h"
#include "wayland/shared-memory.h"
#include <assert.h>
#include <ext-image-capture-source-client.h>
#include <ext-image-copy-capture-client.h>
#include <wayland-client-protocol.h>
#include <wayland-util.h>

typedef struct {
    // Image format
    enum wl_shm_format selected_format;
    uint32_t width;
    uint32_t height;
    bool has_selected_format;
    // associated Wayland objects
    struct ext_image_capture_source_v1 *source;
    struct ext_image_copy_capture_session_v1 *session;
    SharedBuffer *buffer;
    // callback
    WrappedOutput *output;
    OutputCaptureCallback image_callback;
    void *user_data;
    // this context is passed as user data to two different listeners
    // so refcounting is necessary
    int ref_count;
    Image *result;
} FrameContext;

static void frame_context_unref(FrameContext *context) {
    assert(context->ref_count > 0);
    context->ref_count--;
    if (context->ref_count == 0) {
        context->image_callback(context->result, context->user_data);

        if (context->buffer) {
            shared_buffer_destroy(context->buffer);
        }
        if (context->source) {
            ext_image_capture_source_v1_destroy(context->source);
        }
        if (context->session) {
            ext_image_copy_capture_session_v1_destroy(context->session);
        }
        free(context);
    }
}

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
    void *data,
    struct ext_image_copy_capture_frame_v1 *frame,
    uint32_t /* reason */
) {
    FrameContext *context = data;
    ext_image_copy_capture_frame_v1_destroy(frame);
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

    uint32_t stride = image_format_default_stride(
        image_format_from_wl(context->selected_format), context->width
    );
    context->result = image_new_from_wayland(
        context->selected_format,
        context->buffer->data,
        context->width,
        context->height,
        stride
    );

    // This deletes BOTH objects which have a reference to the frame context.
    // So, unref twice!
    ext_image_copy_capture_session_v1_destroy(context->session);
    context->session = NULL;
    frame_context_unref(context);
    ext_image_copy_capture_frame_v1_destroy(frame);
    frame_context_unref(context);
}

static void frame_handle_transform(
    void * /* data */,
    struct ext_image_copy_capture_frame_v1 * /* frame */,
    uint32_t transform
) {
    if (transform != WL_OUTPUT_TRANSFORM_NORMAL) {
        // TODO
        // Unfortunately I don't have a good way to test these.
        report_error_fatal(
            "captured image was transformed and couldn't be handled\n"
        );
    }
}

static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
    .damage = frame_handle_damage,
    .failed = frame_handle_failed,
    .presentation_time = frame_handle_presentation_time,
    .ready = frame_handle_ready,
    .transform = frame_handle_transform,
};

static void session_handle_buffer_size(
    void *data,
    struct ext_image_copy_capture_session_v1 * /* session */,
    uint32_t width,
    uint32_t height
) {
    FrameContext *context = data;
    context->width = width;
    context->height = height;
}

static void session_handle_dmabuf_device(
    void * /* data */,
    struct ext_image_copy_capture_session_v1 * /* session */,
    struct wl_array * /* device */
) {
    // we don't do dmabufs
}

static void session_handle_dmabuf_format(
    void * /* data */,
    struct ext_image_copy_capture_session_v1 * /* session */,
    uint32_t /* format */,
    struct wl_array * /* modifiers */
) {
    // we don't do dmabufs
}

static void session_handle_shm_format(
    void *data,
    struct ext_image_copy_capture_session_v1 * /* session */,
    uint32_t format
) {
    FrameContext *context = data;

    log_debug("got buffer format %x\n", format);

    // 10-bit should be preferred if available
    if (format == WL_SHM_FORMAT_XRGB2101010 ||
        format == WL_SHM_FORMAT_XBGR2101010) {
        goto accept_format;
    } else if ((format == WL_SHM_FORMAT_XRGB8888 ||
                format == WL_SHM_FORMAT_XBGR8888) &&
               !context->has_selected_format) {
        goto accept_format;
    }
    log_debug("skipping\n");
    return;
accept_format:
    context->selected_format = format;
    context->has_selected_format = true;
}

static void session_handle_done(
    void *data, struct ext_image_copy_capture_session_v1 *session
) {
    FrameContext *context = data;
    assert(
        context->width > 0 && context->height > 0 &&
        context->has_selected_format
    );

    struct ext_image_copy_capture_frame_v1 *frame =
        ext_image_copy_capture_session_v1_create_frame(session);

    uint32_t stride = image_format_default_stride(
        image_format_from_wl(context->selected_format), context->width
    );
    context->buffer = shared_buffer_new(
        context->width, context->height, stride, context->selected_format
    );
    assert(context->buffer);

    context->ref_count++;
    ext_image_copy_capture_frame_v1_add_listener(
        frame, &frame_listener, context
    );
    ext_image_copy_capture_frame_v1_attach_buffer(
        frame, context->buffer->wl_buffer
    );
    ext_image_copy_capture_frame_v1_damage_buffer(
        frame, 0, 0, context->width, context->height
    );
    ext_image_copy_capture_frame_v1_capture(frame);
}

static void session_handle_stopped(
    void *data, struct ext_image_copy_capture_session_v1 * /* session */
) {
    FrameContext *context = data;
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
    WrappedOutput *output, OutputCaptureCallback image_callback, void *data
) {

    FrameContext *context = calloc(1, sizeof(FrameContext));
    context->output = output;
    context->image_callback = image_callback;
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

bool capture_output_ext_is_available() {
    return wayland_globals.ext_output_capture_source_manager != NULL &&
           wayland_globals.ext_image_copy_capture_manager != NULL;
}
