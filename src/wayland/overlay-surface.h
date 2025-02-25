#pragma once
#include "fractional-scale-client.h"
#include "wayland/output.h"
#include "wayland/render.h"
#include <cairo.h>
#include <wayland-client.h>
#include <wlr-layer-shell-client.h>

constexpr size_t OVERLAY_SURFACE_BUFFER_COUNT = 2;

typedef void (*OverlaySurfaceDrawCallback)(void *user_data, cairo_t *cr);

typedef struct {
    struct wl_surface *wl_surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wp_viewport *viewport;
    struct wp_fractional_scale_v1 *fractional_scale;
    // scale is multiplied by 120
    uint32_t scale;
    /** The width of the surface in global coordinates. */
    uint32_t logical_width;
    /** The height of the surface in global coordinates. */
    uint32_t logical_height;
    /** The width of the render buffer. */
    uint32_t device_width;
    /** The height of the render buffer. */
    uint32_t device_height;
    RenderBuffer *buffers[OVERLAY_SURFACE_BUFFER_COUNT];
    // callback things
    OverlaySurfaceDrawCallback draw_callback;
    void *user_data;
} OverlaySurface;

OverlaySurface *overlay_surface_new(
    WrappedOutput *output,
    OverlaySurfaceDrawCallback draw_callback,
    void *user_data
);
/** Call the draw_callback immediately and present the result. */
void overlay_surface_draw(OverlaySurface *surface);
