#pragma once
#include "ui/label.h"
#include "ui/tooltip.h"

typedef struct PickerSurface PickerSurface;

typedef enum {
    OUTPUT_PICKER_INACTIVE,
    OUTPUT_PICKER_ACTIVE,
} OutputPickerState;

typedef struct OutputPicker {
    WidgetHeader header;
    OutputPickerState state;
    char *output_name;
    WidgetTooltip output_tooltip;
} OutputPicker;

extern const WidgetVTable WIDGET_OUTPUT_PICKER_VTABLE;

// All other functions are in the vtable.
void output_picker_destroy(PickerSurface *picker);
