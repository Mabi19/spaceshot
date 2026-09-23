#pragma once
#include "wayland/seat.h"

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

void output_picker_init(PickerSurface *picker, WrappedOutput *output);
void output_picker_enter(PickerSurface *picker);
void output_picker_destroy(PickerSurface *picker);
void output_picker_draw(PickerSurface *picker, RenderDisplayList *dl);
void output_picker_handle_mouse(PickerSurface *picker, MouseEvent event);
void output_picker_handle_scale(PickerSurface *picker, uint32_t scale);
