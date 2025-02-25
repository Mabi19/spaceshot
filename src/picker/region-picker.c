#include "picker/region-picker.h"
#include "cairo.h"
#include "image.h"
#include "wayland/output.h"
#include "wayland/overlay-surface.h"
#include <stdlib.h>

static void region_picker_draw(void *data, cairo_t *cr) {
    RegionPicker *picker = data;
    cairo_set_source(cr, picker->background_pattern);
    cairo_paint(cr);
}

RegionPicker *region_picker_new(WrappedOutput *output, Image *background) {
    RegionPicker *result = calloc(1, sizeof(RegionPicker));
    result->surface = overlay_surface_new(output, region_picker_draw, result);
    result->state = REGION_PICKER_EMPTY;
    result->background = image_make_cairo_surface(background);
    result->background_pattern =
        cairo_pattern_create_for_surface(result->background);

    return result;
}
