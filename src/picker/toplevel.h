#pragma once
#include "render/command.h"
#include "wayland/screen-capture.h"
#include "wayland/seat.h"

typedef struct PickerSurface PickerSurface;

typedef struct {
    CaptureFrame *frame;
    char *title;
    BBox image_bounds;
    BBox label_background_bounds;
    double label_text_x;
    double label_text_y;
    /** The bounds used for culling and the selection highlight. */
    BBox full_bounds;
    bool visible;
} ToplevelPickerEntry;

typedef struct {
    ToplevelPickerEntry *entries;
    int entry_count;
    // -1 means nothing highlighted
    int highlight_index;
    bool is_keyboard_focus;

    RenderTextStyle label_text_style;
    uint32_t last_width;
    uint32_t last_height;
    uint32_t last_scale;
} ToplevelPicker;

/** captures is a list of CaptureFrame */
void toplevel_picker_init(PickerSurface *picker, struct wl_list *captures);
void toplevel_picker_enter(PickerSurface *picker);
void toplevel_picker_destroy(PickerSurface *picker);
void toplevel_picker_draw(PickerSurface *picker, RenderDisplayList *dl);
void toplevel_picker_handle_mouse(PickerSurface *picker, MouseEvent event);
void toplevel_picker_handle_keyboard(
    PickerSurface *picker, KeyboardEvent event
);
void toplevel_picker_handle_scale(PickerSurface *picker);
