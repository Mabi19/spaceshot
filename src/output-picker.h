#pragma once
#include "picker-common.h"
#include "wayland/label-surface.h"
#include "wayland/overlay-surface.h"

struct OutputPicker;

typedef void (*OutputPickerFinishCallback)(
    struct OutputPicker *picker, PickerFinishReason reason
);

typedef struct OutputPicker {
    OverlaySurface *surface;
    LabelSurface *label;

    Image *background;
    SharedBuffer *background_buf;
    SharedBuffer *background_inactive_buf;

} OutputPicker;

/**
 * Note that the image is _not_ owned by the @c OutputPicker, and needs to stay
 * alive for as long as the RegionPicker does.
 */
OutputPicker *output_picker_new(
    WrappedOutput *output,
    Image *background,
    OutputPickerFinishCallback finish_callback
);
void output_picker_destroy(OutputPicker *picker);
