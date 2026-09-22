#pragma once
#include "wayland/seat.h"

typedef struct PickerSurface PickerSurface;

struct OutputPicker;

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

void output_picker_init(PickerSurface *picker);
void output_picker_destroy(PickerSurface *picker);
void output_picker_draw(PickerSurface *picker, RenderDisplayList *dl);
void output_picker_handle_mouse(PickerSurface *picker, MouseEvent event);
void output_picker_handle_keyboard(PickerSurface *picker, KeyboardEvent event);
void output_picker_recalculate_label_size(
    PickerSurface *picker, uint32_t scale
);
