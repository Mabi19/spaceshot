#pragma once
#include "render/renderer.h"

typedef struct PickerSurface PickerSurface;

typedef enum {
    OUTPUT_PICKER_INACTIVE,
    OUTPUT_PICKER_ACTIVE,
} OutputPickerState;

typedef struct OutputPicker {
    OutputPickerState state;
    char *output_name;
    RenderTextMetrics label_size;
    bool move_label_down;
} OutputPicker;

// All other functions are in the vtable.
void output_picker_destroy(PickerSurface *picker);
