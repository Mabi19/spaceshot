#pragma once
#include "wayland/globals.h"
#include "wayland/shared-memory.h"
#include <wayland-client.h>
#include <wlr-layer-shell-client.h>

typedef struct {
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    uint32_t width;
    uint32_t height;
    SharedPool *buffer_pool;
    /** The buffer currently being shown on screen. */
    struct wl_buffer *current_buffer;
    /** The buffer being drawn to. */
    struct wl_buffer *other_buffer;
} OverlaySurface;

OverlaySurface *overlay_surface_new(WrappedOutput *output);
