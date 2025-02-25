#pragma once
#include "image.h"
#include "wayland/output.h"
#include "wayland/overlay-surface.h"
#include <wayland-client.h>

typedef enum { REGION_PICKER_EMPTY, REGION_PICKER_DRAGGING } RegionPickerState;

typedef struct {
    OverlaySurface *surface;
    RegionPickerState state;
    cairo_surface_t *background;
    cairo_pattern_t *background_pattern;
    // Note that these values are only valid when state != REGION_PICKER_EMPTY.
    // In logical coordinates
    double x1, y1;
    double x2, y2;
} RegionPicker;

/**
 * Note that the image is _not_ owned by the RegionPicker, and needs to stay
 * alive for as long as the RegionPicker does.
 */
RegionPicker *region_picker_new(WrappedOutput *output, Image *background);
void region_picker_destroy(RegionPicker *picker);
