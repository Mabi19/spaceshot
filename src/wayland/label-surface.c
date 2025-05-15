#include "label-surface.h"
#include "log.h"
#include "wayland/globals.h"
#include "wayland/render.h"
#include <assert.h>
#include <cairo.h>
#include <stdlib.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

LabelSurface *label_surface_new(
    struct wl_surface *parent, const char *text, LabelSurfaceStyle style
) {
    LabelSurface *result = calloc(1, sizeof(LabelSurface));
    result->style = style;

    result->wl_surface =
        wl_compositor_create_surface(wayland_globals.compositor);
    result->wl_subsurface = wl_subcompositor_get_subsurface(
        wayland_globals.subcompositor, result->wl_surface, parent
    );

    label_surface_set_text(result, text);
    label_surface_update_layout(result);
    return result;
}

void label_surface_set_text(LabelSurface *label, const char *text) {
    free(label->text);
    label->text = strdup(text);
}

void label_surface_layout(LabelSurface *label) {
    // TODO: These values will need to actually get measured.
    // This is tricky, as measuring needs a surface, and that can only be
    // created once the size is known.
    uint32_t text_width = 100;
    uint32_t text_height = 10;

    uint32_t new_device_width = text_width + 2.0 * label->style.padding;
    uint32_t new_device_height = text_height + 2.0 * label->style.padding;

    if (new_device_width != label->device_width ||
        new_device_height != label->device_height) {
        if (label->buffer) {
            render_buffer_destroy(label->buffer);
        }
        label->buffer = render_buffer_new(
            new_device_width, new_device_height, IMAGE_FORMAT_ARGB8888
        );
    }

    label->device_width = new_device_width;
    label->device_height = new_device_height;
}

void label_surface_draw(LabelSurface *label) {
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
    // TODO: multiply by scale
    double r = label->style.corner_radius;
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

    // TODO: draw the text
    // for now, box
    double padding = label->style.padding;
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_rectangle(
        cr, padding, padding, d_width - 2 * padding, d_height - 2 * padding
    );
    cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
    cairo_fill(cr);

    cairo_surface_flush(label->buffer->cairo_surface);
    TIMING_END(label_render);
}

void label_surface_destroy(LabelSurface *label) {
    g_object_unref(label->layout);
    free(label->text);
    render_buffer_destroy(label->buffer);
    wl_subsurface_destroy(label->wl_subsurface);
    wl_surface_destroy(label->wl_surface);
}
