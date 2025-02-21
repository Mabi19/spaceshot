#pragma once
#include "wayland/shared-memory.h"
#include <cairo.h>

/**
 * Wrapper around a SharedBuffer, made for drawing with cairo.
 * Always XRGB8888.
 */
typedef struct {
    SharedBuffer *shm;
    cairo_surface_t *cairo_surface;
    cairo_t *cr;
    bool is_busy;
} RenderBuffer;

RenderBuffer *render_buffer_new(uint32_t width, uint32_t height);
/** Convenience function to automatically set the is_busy flag */
void render_buffer_attach_to_surface(
    RenderBuffer *buffer, struct wl_surface *surface
);
void render_buffer_destroy(RenderBuffer *buffer);
