#include "overlay-surface.h"
#include "wayland/globals.h"
#include "wayland/render.h"
#include "wlr-layer-shell-client.h"
#include <assert.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

static RenderBuffer *get_unused_buffer(OverlaySurface *window) {
    // first, try to get an existing buffer
    for (size_t i = 0; i < OVERLAY_SURFACE_BUFFER_COUNT; i++) {
        if (!window->buffers[i]) {
            continue;
        }
        RenderBuffer *test_buf = window->buffers[i];
        if (!test_buf->is_busy && test_buf->shm->width == window->width &&
            test_buf->shm->height == window->height) {
            printf("returned buffer #%zu\n", i);
            return test_buf;
        }
    }

    // second, try to create a new one in an empty spot
    // or overwrite one with the wrong size
    for (size_t i = 0; i < OVERLAY_SURFACE_BUFFER_COUNT; i++) {
        if (window->buffers[i]) {
            if (window->buffers[i]->shm->width == window->width &&
                window->buffers[i]->shm->height == window->height) {
                continue;
            }
            printf("destroyed buffer #%zu\n", i);
            render_buffer_destroy(window->buffers[i]);
        }
        window->buffers[i] = render_buffer_new(window->width, window->height);
        printf("created buffer #%zu\n", i);
        return window->buffers[i];
    }

    // last resort: overwrite the first one
    if (window->buffers[0]) {
        render_buffer_destroy(window->buffers[0]);
    }
    window->buffers[0] = render_buffer_new(window->width, window->height);
    printf("overwrote buffer #0 (last resort)\n");

    return window->buffers[0];
}

static void overlay_surface_handle_configure(
    void *data,
    struct zwlr_layer_surface_v1 * /* layer_surface */,
    uint32_t serial,
    uint32_t width,
    uint32_t height
) {
    OverlaySurface *window = data;
    printf(
        "Received configure for surface %p with w = %d, h = %d\n",
        data,
        width,
        height
    );

    zwlr_layer_surface_v1_ack_configure(window->layer_surface, serial);

    window->width = width;
    window->height = height;

    RenderBuffer *draw_buf = get_unused_buffer(window);
    cairo_set_source_rgb(draw_buf->cr, 0.5, 0.5, 0.5);
    cairo_paint(draw_buf->cr);
    cairo_surface_flush(draw_buf->cairo_surface);

    render_buffer_attach_to_surface(draw_buf, window->surface);
    wl_surface_commit(window->surface);
}

static void overlay_surface_handle_closed(
    void *data, struct zwlr_layer_surface_v1 * /* layer_surface */
) {
    printf("Overlay surface %p closed\n", data);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = overlay_surface_handle_configure,
    .closed = overlay_surface_handle_closed,
};

OverlaySurface *overlay_surface_new(WrappedOutput *output) {
    OverlaySurface *result = calloc(1, sizeof(OverlaySurface));
    result->surface = wl_compositor_create_surface(wayland_globals.compositor);
    result->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        wayland_globals.layer_shell,
        result->surface,
        output->wl_output,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
        "spaceshot"
    );
    zwlr_layer_surface_v1_add_listener(
        result->layer_surface, &layer_surface_listener, result
    );

    const enum zwlr_layer_surface_v1_anchor ANCHOR =
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
    zwlr_layer_surface_v1_set_anchor(result->layer_surface, ANCHOR);
    // do not honor other surfaces' exclusive zones
    zwlr_layer_surface_v1_set_exclusive_zone(result->layer_surface, -1);
    wl_surface_commit(result->surface);

    return result;
}
