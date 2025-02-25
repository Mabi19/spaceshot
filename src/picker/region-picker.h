#pragma once
#include "wayland/output.h"
#include "wayland/overlay-surface.h"
#include <wayland-client.h>

typedef enum { REGION_PICKER_EMPTY, REGION_PICKER_DRAGGING } RegionPickerState;

typedef struct {
    OverlaySurface *surface;
    RegionPickerState state;
} RegionPicker;

RegionPicker *region_picker_new(WrappedOutput *output);
void region_picker_destroy(RegionPicker *picker);
