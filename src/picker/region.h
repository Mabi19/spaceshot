#pragma once
#include "render/command.h"
#include "wayland/seat.h"

typedef struct PickerSurface PickerSurface;

typedef enum {
    REGION_PICKER_EMPTY,
    REGION_PICKER_DRAGGING,
    REGION_PICKER_EDITING,
} RegionPickerState;

typedef struct {
    RegionPickerState state;
    // Note that these values are only valid when state != REGION_PICKER_EMPTY.
    // In logical coordinates
    double x1, y1;
    double x2, y2;
    // holding Space or Alt moves the region instead of resizing it
    bool move_flag;
    // holding Ctrl when releasing changes into edit mode instead of finishing
    bool edit_flag;
    struct {
        bool is_move;
        // These are the corners to modify when is_move is false.
        double *modify_x;
        double *modify_y;
        // If is_move is false, these are the offset from the grabbed
        // corner/edge, and if is_move is true, these are the offset between the
        // grab point and (x1, y1)
        double grab_offset_x;
        double grab_offset_y;
    } edit_data;
} RegionPicker;

void region_picker_init(PickerSurface *picker);
void region_picker_draw(PickerSurface *picker, RenderDisplayList *dl);
void region_picker_handle_mouse(PickerSurface *picker, MouseEvent event);
void region_picker_handle_keyboard(PickerSurface *picker, KeyboardEvent event);
