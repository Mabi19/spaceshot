#pragma once
#include "wayland/render.h"
#include <pango/pangocairo.h>
#include <wayland-client.h>

typedef enum {
    LABEL_SURFACE_ANCHOR_CENTER = 0,
    LABEL_SURFACE_ANCHOR_TOP = 1,
    LABEL_SURFACE_ANCHOR_BOTTOM = 2,
    LABEL_SURFACE_ANCHOR_LEFT = 4,
    LABEL_SURFACE_ANCHOR_RIGHT = 8,
    LABEL_SURFACE_ANCHOR_TOP_LEFT =
        LABEL_SURFACE_ANCHOR_TOP | LABEL_SURFACE_ANCHOR_LEFT,
    LABEL_SURFACE_ANCHOR_TOP_RIGHT =
        LABEL_SURFACE_ANCHOR_TOP | LABEL_SURFACE_ANCHOR_RIGHT,
    LABEL_SURFACE_ANCHOR_BOTTOM_LEFT =
        LABEL_SURFACE_ANCHOR_BOTTOM | LABEL_SURFACE_ANCHOR_LEFT,
    LABEL_SURFACE_ANCHOR_BOTTOM_RIGHT =
        LABEL_SURFACE_ANCHOR_BOTTOM | LABEL_SURFACE_ANCHOR_RIGHT,
} LabelSurfaceAnchor;

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
    LabelSurfaceStyle style;
    PangoLayout *layout;
    PangoFontDescription *font_description;
    bool visible;

    // This pair of coordinates is for the top-left corner.
    int32_t x;
    int32_t y;
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
/**
 * Hide the label surface (by attaching a null buffer to the surface).
 * This function calls wl_surface_commit.
 */
void label_surface_hide(LabelSurface *label);
/**
 * Moves the label surface. It will be positioned such that the specified
 * position in the parent's coordinate space is at the specified anchor.
 */
void label_surface_set_position(
    LabelSurface *label, int32_t x, int32_t y, LabelSurfaceAnchor anchor
);

void label_surface_destroy(LabelSurface *label);
