#include "screenshot.h"
#include "image.h"
#include "wayland/globals.h"
#include "wayland/shared-memory.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <wayland-client-protocol.h>
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
    int fd;
    struct wl_shm_pool *pool;
    struct wl_buffer *wayland_buffer;
    uint8_t *buffer_data;
    uint32_t buffer_size;
    // callback
    ScreenshotCallback image_callback;
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
    context->buffer_size = context->stride * context->height;
    context->fd = create_shm_fd(context->buffer_size);
    // TODO: figure out if PROT_WRITE is really necessary here
    context->buffer_data = mmap(
        NULL,
        context->buffer_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        context->fd,
        0
    );

    context->pool = wl_shm_create_pool(
        wayland_globals.shm, context->fd, context->buffer_size
    );
    context->wayland_buffer = wl_shm_pool_create_buffer(
        context->pool,
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

    Image *result = image_create_from_wayland(
        context->selected_format,
        context->buffer_data,
        context->width,
        context->height,
        context->stride
    );
    // callback's here
    context->image_callback(result);

    // cleanup
    zwlr_screencopy_frame_v1_destroy(frame);
    munmap(context->buffer_data, context->buffer_size);
    destroy_shm_fd(context->fd);
    wl_buffer_destroy(context->wayland_buffer);
    wl_shm_pool_destroy(context->pool);
}

static const struct zwlr_screencopy_frame_v1_listener frame_listener = {
    // buffer setup events
    .buffer = frame_handle_buffer,
    .buffer_done = frame_handle_buffer_done,
    .linux_dmabuf = frame_handle_linux_dmabuf,
    // buffer copy event
    .flags = frame_handle_flags,
    .ready = frame_handle_ready,
};

void take_output_screenshot(
    struct wl_output *output, ScreenshotCallback image_callback
) {
    struct zwlr_screencopy_frame_v1 *frame =
        zwlr_screencopy_manager_v1_capture_output(
            wayland_globals.screencopy_manager, 0, output
        );
    FrameContext *context = calloc(1, sizeof(FrameContext));
    context->image_callback = image_callback;
    zwlr_screencopy_frame_v1_add_listener(frame, &frame_listener, context);
}
