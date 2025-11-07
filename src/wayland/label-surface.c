#include "label-surface.h"
#include "log.h"
#include "wayland/globals.h"
#include "wayland/render.h"
#include <assert.h>
#include <cairo.h>
#include <fractional-scale-client.h>
#include <pango/pangocairo.h>
#include <stdlib.h>
#include <viewporter-client.h>
#include <wayland-client.h>

/**
 * Measure a piece of text. This is done with an external @c PangoLayout for
 * simplicity.
 */
static void measure_text(
    const char *text,
    PangoFontDescription *font_description,
    int *width,
    int *height
) {
    static cairo_surface_t *dummy_surface = NULL;
    static cairo_t *dummy_cr = NULL;
    static PangoLayout *layout = NULL;

    if (!layout) {
        dummy_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
        dummy_cr = cairo_create(dummy_surface);
        layout = pango_cairo_create_layout(dummy_cr);
    }
    pango_layout_set_text(layout, text, -1);
    pango_layout_set_font_description(layout, font_description);
    pango_layout_get_pixel_size(layout, width, height);
}

static void label_surface_update(LabelSurface *label) {
    int text_width, text_height;
    measure_text(
        label->text, label->font_description, &text_width, &text_height
    );
    log_debug("measured size: %d %d\n", text_width, text_height);

    double padding_x =
        config_length_to_pixels(label->style.padding_x, label->scale);
    double padding_y =
        config_length_to_pixels(label->style.padding_y, label->scale);

    uint32_t new_device_width = text_width + 2 * padding_x;
    uint32_t new_device_height = text_height + 2 * padding_y;

    if (new_device_width != label->device_width ||
        new_device_height != label->device_height) {
        if (label->buffer) {
            render_buffer_destroy(label->buffer);
        }
        log_debug(
            "creating label buffer of size %d, %d at scale %f, fd size %d\n",
            new_device_width,
            new_device_height,
            label->scale,
            pango_font_description_get_size(label->font_description) /
                PANGO_SCALE
        );
        label->buffer = render_buffer_new(
            new_device_width, new_device_height, IMAGE_FORMAT_ARGB8888
        );

        if (label->layout) {
            pango_cairo_update_layout(label->buffer->cr, label->layout);
        } else {
            label->layout = pango_cairo_create_layout(label->buffer->cr);
        }

        // slightly awkward: the logical size isn't tracked anywhere, so divide
        wp_viewport_set_destination(
            label->viewport,
            (new_device_width * 120.0) / label->scale,
            (new_device_height * 120.0) / label->scale
        );
    }
    pango_layout_set_text(label->layout, label->text, -1);
    pango_layout_set_font_description(label->layout, label->font_description);

    label->device_width = new_device_width;
    label->device_height = new_device_height;

    // draw
    assert(label->buffer);
    TIMING_START(label_render);

    cairo_t *cr = label->buffer->cr;
    // clear
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.0);
    cairo_paint(cr);

    // background
    cairo_set_source_config_color(
        cr, label->style.background_color, IMAGE_FORMAT_ARGB8888
    );
    double r = (label->style.corner_radius * label->scale) / 120.0;
    double d_width = label->device_width;
    double d_height = label->device_height;

    const double PI = 3.141592653589793115997963468544185161590576171875;
    const double DEG = PI / 180.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, d_width - r, r, r, -90 * DEG, 0 * DEG);
    cairo_arc(cr, d_width - r, d_height - r, r, 0 * DEG, 90 * DEG);
    cairo_arc(cr, r, d_height - r, r, 90 * DEG, 180 * DEG);
    cairo_arc(cr, r, r, r, 180 * DEG, 270 * DEG);
    cairo_close_path(cr);
    cairo_fill(cr);

    // text
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_config_color(
        cr, label->style.text_color, IMAGE_FORMAT_ARGB8888
    );
    cairo_move_to(cr, padding_x, padding_y);
    pango_cairo_show_layout(cr, label->layout);

    cairo_surface_flush(label->buffer->cairo_surface);
    TIMING_END(label_render);

    if (label->visible) {
        render_buffer_attach_to_surface(label->buffer, label->wl_surface);
        wl_surface_commit(label->wl_surface);
    }
}

static void preferred_scale_changed(
    void *data,
    struct wp_fractional_scale_v1 * /* fractional_scale */,
    uint32_t new_scale
) {
    LabelSurface *label = data;
    log_debug("label fractional scale %u\n", new_scale);
    if (label->scale != new_scale) {
        label->scale = new_scale;
        pango_font_description_set_absolute_size(
            label->font_description,
            config_length_to_pixels(label->style.font_size, new_scale) *
                PANGO_SCALE
        );
        label_surface_update(label);
    }
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener =
    {.preferred_scale = preferred_scale_changed};

LabelSurface *label_surface_new(
    struct wl_surface *parent, const char *text, LabelSurfaceStyle style
) {
    LabelSurface *result = calloc(1, sizeof(LabelSurface));

    result->wl_surface =
        wl_compositor_create_surface(wayland_globals.compositor);
    result->wl_subsurface = wl_subcompositor_get_subsurface(
        wayland_globals.subcompositor, result->wl_surface, parent
    );
    result->viewport = wp_viewporter_get_viewport(
        wayland_globals.viewporter, result->wl_surface
    );
    result->scale = 120;
    result->fractional_scale =
        wp_fractional_scale_manager_v1_get_fractional_scale(
            wayland_globals.fractional_scale_manager, result->wl_surface
        );
    wp_fractional_scale_v1_add_listener(
        result->fractional_scale, &fractional_scale_listener, result
    );

    // Prevent mouse movement from going to the label's surface.
    struct wl_region *empty_region =
        wl_compositor_create_region(wayland_globals.compositor);
    wl_surface_set_input_region(result->wl_surface, empty_region);
    wl_region_destroy(empty_region);

    result->text = strdup(text);
    result->style = style;
    result->font_description = pango_font_description_new();
    pango_font_description_set_family(
        result->font_description, style.font_family
    );
    pango_font_description_set_absolute_size(
        result->font_description,
        style.font_size.value * PANGO_SCALE * result->scale / 120.0
    );

    label_surface_update(result);
    return result;
}

void label_surface_set_text(LabelSurface *label, const char *text) {
    if (strcmp(label->text, text) != 0) {
        free(label->text);
        label->text = strdup(text);
        label_surface_update(label);
    }
}

void label_surface_show(LabelSurface *label) {
    if (label->visible) {
        return;
    }
    label->visible = true;

    render_buffer_attach_to_surface(label->buffer, label->wl_surface);
    wl_surface_commit(label->wl_surface);
}

void label_surface_hide(LabelSurface *label) {
    if (!label->visible) {
        return;
    }
    label->visible = false;
    wl_surface_attach(label->wl_surface, NULL, 0, 0);
    wl_surface_commit(label->wl_surface);
}

void label_surface_set_position(
    LabelSurface *label, int32_t x, int32_t y, Anchor anchor
) {
    int32_t width = label->device_width * label->scale / 120.0;
    int32_t height = label->device_width * label->scale / 120.0;

    int32_t tl_x, tl_y;

    if (anchor & ANCHOR_LEFT) {
        tl_x = x;
    } else if (anchor & ANCHOR_RIGHT) {
        tl_x = x - width;
    } else {
        tl_x = x - width / 2;
    }

    if (anchor & ANCHOR_TOP) {
        tl_y = y;
    } else if (anchor & ANCHOR_RIGHT) {
        tl_y = y - height;
    } else {
        tl_y = y - height / 2;
    }

    if (label->x == tl_x && label->y == tl_y) {
        return;
    }

    label->x = tl_x;
    label->y = tl_y;

    wl_subsurface_set_position(label->wl_subsurface, tl_x, tl_y);
}

void label_surface_destroy(LabelSurface *label) {
    g_object_unref(label->layout);
    pango_font_description_free(label->font_description);
    free(label->text);
    render_buffer_destroy(label->buffer);
    wl_subsurface_destroy(label->wl_subsurface);
    wl_surface_destroy(label->wl_surface);
}
