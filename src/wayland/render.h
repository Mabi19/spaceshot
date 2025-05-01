#pragma once
#include "image.h"
#include "wayland/shared-memory.h"
#include <cairo.h>
#include <config/config.h>

// Render buffers, and miscellaneous functions related to rendering.

/**
 * Wrapper around a SharedBuffer, made for drawing with cairo.
 */
typedef struct {
    SharedBuffer *shm;
    cairo_surface_t *cairo_surface;
    cairo_t *cr;
    bool is_busy;
} RenderBuffer;

RenderBuffer *
render_buffer_new(uint32_t width, uint32_t height, ImageFormat format);
/** Convenience function to automatically set the is_busy flag */
void render_buffer_attach_to_surface(
    RenderBuffer *buffer, struct wl_surface *surface
);
void render_buffer_destroy(RenderBuffer *buffer);

void cairo_set_source_config_color(
    cairo_t *cr, ConfigColor color, ImageFormat surface_format
);

/**
 * Convert a ConfigLength to device pixels, keeping in mind surface scale.
 * The value is rounded to the nearest pixel.
 */
int config_length_to_pixels(ConfigLength length, uint32_t surface_scale);
