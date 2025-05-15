#pragma once
#include "wayland/render.h"
#include <pango/pangocairo.h>
#include <wayland-client.h>

typedef struct {
    PangoFontDescription *font_description;
    ConfigColor text_color;
    ConfigColor background_color;
    double padding;
    double corner_radius;
} LabelSurfaceStyle;

/** A subsurface that renders text, optionally with a background. */
typedef struct {
    struct wl_surface *wl_surface;
    struct wl_subsurface *wl_subsurface;
    RenderBuffer *buffer;
    char *text;
    // Note that the LabelSurface does not own the PangoFontDescription within.
    LabelSurfaceStyle style;
    PangoLayout *layout;

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
 * Note that you'll need to call label_surface_draw to make this take effect.
 */
void label_surface_set_text(LabelSurface *label, const char *text);
/**
 * This needs to be called after every text update.
 */
void label_surface_update_layout(LabelSurface *label);
/**
 * Render the contents of the label.
 * Note that this function doesn't actually attach the newly filled-in buffer,
 * nor does it call wl_surface_commit.
 */
void label_surface_draw(LabelSurface *label);
void label_surface_destroy(LabelSurface *label);
