#include "screenshot.h"
#include "image.h"
#include "log.h"
#include "wayland/globals.h"
#include "wayland/shared-memory.h"
#include <stdbool.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <wlr-screencopy-client.h>

typedef struct {
    // Image format
    enum wl_shm_format selected_format;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    bool has_selected_format;
    enum zwlr_screencopy_frame_v1_flags frame_flags;
    // Shared memory bookkeeping
    SharedBuffer *buffer;
    // callback
    WrappedOutput *output;
    ScreenshotCallback image_callback;
    void *user_data;
} FrameContext;

static void frame_handle_buffer(
    void *data,
    struct zwlr_screencopy_frame_v1 * /* frame */,
    uint32_t format,
    uint32_t width,
    uint32_t height,
    uint32_t stride
) {
    FrameContext *context = data;

    log_debug(
        "got buffer format %x, %dx%d, stride = %d\n",
        format,
        width,
        height,
        stride
    );

    // 10-bit should be preferred if available
    if (format == WL_SHM_FORMAT_XRGB2101010 ||
        format == WL_SHM_FORMAT_XBGR2101010) {
        goto accept_format;
    } else if (format == WL_SHM_FORMAT_XRGB8888 &&
               !context->has_selected_format) {
        goto accept_format;
    }
    log_debug("skipping\n");
    return;
accept_format:
    context->selected_format = format;
    context->width = width;
    context->height = height;
    context->stride = stride;
    context->has_selected_format = true;
}

static void frame_handle_linux_dmabuf(
    void * /* data */,
    struct zwlr_screencopy_frame_v1 * /* frame */,
    uint32_t /* format */,
    uint32_t /* width */,
    uint32_t /* height */
) {
    log_debug("got linux_dmabuf, doing nothing with it\n");
}

static void
frame_handle_buffer_done(void *data, struct zwlr_screencopy_frame_v1 *frame) {
    FrameContext *context = data;

    if (!context->has_selected_format) {
        report_error_fatal("couldn't agree on screenshot image format");
    }

    context->buffer = shared_buffer_new(
        context->width,
        context->height,
        context->stride,
        context->selected_format
    );
    zwlr_screencopy_frame_v1_copy(frame, context->buffer->wl_buffer);

    log_debug("buffer done\n");
}

static void frame_handle_flags(
    void *data,
    struct zwlr_screencopy_frame_v1 * /* frame */,
    enum zwlr_screencopy_frame_v1_flags flags
) {
    FrameContext *context = data;
    context->frame_flags = flags;
}

static void frame_handle_ready(
    void *data,
    struct zwlr_screencopy_frame_v1 *frame,
    uint32_t /* tv_sec_hi */,
    uint32_t /* tv_sec_lo */,
    uint32_t /* tv_nsec */
) {
    FrameContext *context = data;

    Image *result = image_new_from_wayland(
        context->selected_format,
        context->buffer->data,
        context->width,
        context->height,
        context->stride
    );
    log_debug(
        "Got image from wayland: %dx%d, %d bytes in total\n",
        result->width,
        result->height,
        result->stride * result->height
    );
    log_debug(
        "Top-left pixel: %x, original: %x\n",
        ((uint32_t *)result->data)[0],
        ((uint32_t *)context->buffer->data)[0]
    );
    context->image_callback(context->output, result, context->user_data);

    // cleanup
    zwlr_screencopy_frame_v1_destroy(frame);
    shared_buffer_destroy(context->buffer);
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
    // buffer setup events
    .buffer = frame_handle_buffer,
    .buffer_done = frame_handle_buffer_done,
    .linux_dmabuf = frame_handle_linux_dmabuf,
    .flags = frame_handle_flags,
    // buffer copy event
    .ready = frame_handle_ready,
};

void take_output_screenshot(
    WrappedOutput *output, ScreenshotCallback image_callback, void *data
) {
    struct zwlr_screencopy_frame_v1 *frame =
        zwlr_screencopy_manager_v1_capture_output(
            wayland_globals.screencopy_manager, 0, output->wl_output
        );
    FrameContext *context = calloc(1, sizeof(FrameContext));
    context->output = output;
    context->image_callback = image_callback;
    context->user_data = data;
    zwlr_screencopy_frame_v1_add_listener(frame, &frame_listener, context);
}
