#include "screenshot.h"
#include "image.h"
#include "wayland/globals.h"
#include "wayland/shared-memory.h"
#include <stdbool.h>
#include <stdio.h>
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
    SharedPool *pool;
    struct wl_buffer *wayland_buffer;
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

    printf(
        "got buffer format %x, %dx%d, stride = %d\n",
        format,
        width,
        height,
        stride
    );

    if (context->has_selected_format) {
        printf("skipping\n");
    } else {
        if (format == WL_SHM_FORMAT_XRGB8888) {
            context->selected_format = format;
            context->width = width;
            context->height = height;
            context->stride = stride;
            context->has_selected_format = true;
        }
    }
}

static void frame_handle_linux_dmabuf(
    void * /* data */,
    struct zwlr_screencopy_frame_v1 * /* frame */,
    uint32_t /* format */,
    uint32_t /* width */,
    uint32_t /* height */
) {
    printf("got linux_dmabuf, doing nothing with it\n");
}

static void
frame_handle_buffer_done(void *data, struct zwlr_screencopy_frame_v1 *frame) {
    FrameContext *context = data;

    if (!context->has_selected_format) {
        fprintf(stderr, "Couldn't agree on image format\n");
        exit(EXIT_FAILURE);
    }

    // create a buffer
    context->pool = shared_pool_new(context->stride * context->height);
    context->wayland_buffer = wl_shm_pool_create_buffer(
        context->pool->wl_pool,
        0,
        context->width,
        context->height,
        context->stride,
        context->selected_format
    );

    // copy the frame into the buffer
    zwlr_screencopy_frame_v1_copy(frame, context->wayland_buffer);

    printf("buffer done\n");
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
        context->pool->data,
        context->width,
        context->height,
        context->stride
    );
    context->image_callback(context->output, result, context->user_data);

    // cleanup
    zwlr_screencopy_frame_v1_destroy(frame);
    wl_buffer_destroy(context->wayland_buffer);
    shared_pool_destroy(context->pool);
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
