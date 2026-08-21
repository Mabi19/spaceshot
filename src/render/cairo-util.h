#pragma once
#include "image.h"
#include <cairo.h>
#include <config/config.h>

void cairo_set_source_config_color(
    cairo_t *cr, ConfigColor color, ImageFormat surface_format
);
