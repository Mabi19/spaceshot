#include "cairo-util.h"

// TODO: move all of these functions out:
// cairo_set_source_config_color will be the internals of the cairo renderer

void cairo_set_source_config_color(
    cairo_t *cr, ConfigColor color, ImageFormat surface_format
) {
    // cairo only supports RGB order, so trick it if necessary
    if (surface_format & IMAGE_FORMAT_FLIPPED_ORDER) {
        cairo_set_source_rgba(cr, color.b, color.g, color.r, color.a);
    } else {
        cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    }
}
