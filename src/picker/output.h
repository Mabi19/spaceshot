#pragma once
typedef struct PickerSurface PickerSurface;

typedef enum {
    OUTPUT_PICKER_INACTIVE,
    OUTPUT_PICKER_ACTIVE,
} OutputPickerState;

typedef struct OutputPicker {
    OutputPickerState state;
    char *output_name;
    // TODO: Make this thing
    WidgetTooltip output_tooltip;
} OutputPicker;

// All other functions are in the vtable.
void output_picker_destroy(PickerSurface *picker);
