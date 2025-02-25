#include "picker/region-picker.h"
#include "wayland/output.h"
#include "wayland/overlay-surface.h"
#include <stdlib.h>

static void region_picker_draw(void *data, cairo_t *cr) {
    // RegionPicker *picker = data;
    cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
    cairo_paint(cr);
}

RegionPicker *region_picker_new(WrappedOutput *output) {
    RegionPicker *result = calloc(1, sizeof(RegionPicker));
    result->surface = overlay_surface_new(output, region_picker_draw, result);
    result->state = REGION_PICKER_EMPTY;

    return result;
}
