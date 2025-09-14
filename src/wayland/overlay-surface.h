#pragma once
#include "wayland/output.h"
#include "wayland/render.h"
#include <cairo.h>
#include <cursor-shape-client.h>
#include <fractional-scale-client.h>
#include <wayland-client.h>
#include <wlr-layer-shell-client.h>

constexpr size_t OVERLAY_SURFACE_BUFFER_COUNT = 2;

/**
 * Returns whether the surface contents were actually updated. This should call
 * overlay_surface_damage() itself
 */
typedef bool (*OverlaySurfaceDrawCallback)(void *user_data, cairo_t *cr);

/**
 * Like @c OverlaySurfaceDrawCallback, except that you have to attach a buffer
 * yourself.
 */
typedef void (*OverlaySurfaceManualRenderCallback)(void *user_data);

/**
 * Called when the surface is closed. This should call overlay_surface_destroy.
 */
typedef void (*OverlaySurfaceCloseCallback)(void *user_data);

typedef struct {
    // Only one of {draw, manual_render} can be defined at once.
    OverlaySurfaceDrawCallback draw;
    OverlaySurfaceManualRenderCallback manual_render;
    OverlaySurfaceCloseCallback close;
} OverlaySurfaceHandlers;

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
    /** The window's pixel format. */
    ImageFormat pixel_format;
    RenderBuffer *buffers[OVERLAY_SURFACE_BUFFER_COUNT];
    // callback things
    OverlaySurfaceHandlers handlers;
    // technically also a callback thing.
    // Consumed by the SeatDispatcher
    enum wp_cursor_shape_device_v1_shape cursor_shape;
    void *user_data;
    // used to safeguard against drawing frames after the surface is destroyed
    struct wl_callback *frame_callback;
    // used for frame pacing
    bool has_requested_frame;
    bool has_queued_render;
} OverlaySurface;

OverlaySurface *overlay_surface_new(
    WrappedOutput *output,
    ImageFormat pixel_format,
    OverlaySurfaceHandlers handlers,
    void *user_data
);
/** Call the draw_callback sometime in the future and present the result. */
void overlay_surface_queue_draw(OverlaySurface *surface);
/**
 * Damage the surface in device coordinates. This should be called from the
 * draw callback.
 */
void overlay_surface_damage(OverlaySurface *surface, BBox damage_box);
void overlay_surface_destroy(OverlaySurface *surface);
