#pragma once
#include "wayland/label-surface.h"
#include "wayland/overlay-surface.h"

typedef struct {
    OverlaySurface *surface;
    LabelSurface *label;

} OutputPicker;

/**
 * Note that the image is _not_ owned by the @c OutputPicker, and needs to stay
 * alive for as long as the RegionPicker does.
 */
OutputPicker *output_picker_new(WrappedOutput *output, Image *background);
void output_picker_destroy(OutputPicker *picker);
