#pragma once
#include "wayland/render.h"
#include <pango/pangocairo.h>
#include <wayland-client.h>

// TODO: Remove the requirement to call update_layout() and draw() manually.
// The label should be redrawn automatically when its properties change, or when
// the surface scale changes.

typedef struct {
    const char *font_family;
    ConfigLength font_size;
    ConfigColor text_color;
    ConfigColor background_color;
    ConfigLength padding_x;
    ConfigLength padding_y;
    double corner_radius;
} LabelSurfaceStyle;

/** A subsurface that renders text, optionally with a background. */
typedef struct {
    struct wl_surface *wl_surface;
    struct wl_subsurface *wl_subsurface;
    struct wp_viewport *viewport;
    struct wp_fractional_scale_v1 *fractional_scale;
    double scale;
    RenderBuffer *buffer;
    char *text;
    // Note that the LabelSurface does not own the PangoFontDescription within.
    LabelSurfaceStyle style;
    PangoLayout *layout;
    PangoFontDescription *font_description;

    uint32_t logical_width;
    uint32_t logical_height;
    uint32_t device_width;
    uint32_t device_height;
} LabelSurface;

/**
 * Create a new LabelSurface. The @p text is copied.
 * The @p style.font_description is not. label_surface_update_layout is called
 * automatically.
 */
LabelSurface *label_surface_new(
    struct wl_surface *parent, const char *text, LabelSurfaceStyle style
);
/**
 * Set the text of the LabelSurface. The @p text is copied.
 */
void label_surface_set_text(LabelSurface *label, const char *text);
/**
 * Show the label surface (by attaching its buffer to the surface).
 * This function calls wl_surface_commit.
 */
void label_surface_show(LabelSurface *label);

void label_surface_destroy(LabelSurface *label);
