#include "overlay-surface.h"
#include "wayland/globals.h"
#include "wayland/shared-memory.h"
#include "wlr-layer-shell-client.h"
#include <assert.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-client-protocol.h>

// TODO: Rework how buffers are managed
// Instead of trying to stuff multiple buffers inside one pool, always create
// one buffer per pool. Additionally, make buffer sizes immutable. This means
// that the wl_shm_pool object can be immediately discarded.

// TODO: Hook up cairo drawing
// Cairo seems to be little-endian as well, so cairo ARGB32 = wl ARGB8888

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
    // TODO: Make Cairo decide this
    uint32_t image_buffer_size = width * height * 4;
    if (window->current_buffer || window->other_buffer) {
        // TODO: handle this
        fprintf(stderr, "configure called while buffers already exist");
        exit(EXIT_FAILURE);
    }

    if (window->buffer_pool) {
        assert(
            shared_pool_ensure_size(window->buffer_pool, 2 * image_buffer_size)
        );
    } else {
        window->buffer_pool = shared_pool_new(2 * image_buffer_size);
    }

    window->current_buffer = wl_shm_pool_create_buffer(
        window->buffer_pool->wl_pool,
        0,
        width,
        height,
        width * 4,
        WL_SHM_FORMAT_XRGB8888
    );

    window->other_buffer = wl_shm_pool_create_buffer(
        window->buffer_pool->wl_pool,
        0,
        width,
        height,
        width * 4,
        WL_SHM_FORMAT_XRGB8888
    );
    memset(window->buffer_pool->data, 0x7f, image_buffer_size);
    wl_surface_attach(window->surface, window->current_buffer, 0, 0);

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
