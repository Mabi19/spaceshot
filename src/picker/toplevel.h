#pragma once
#include "render/command.h"
#include "wayland/screen-capture.h"

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
    /**
     * The column this entry is laid out in.
     * Only valid if visible.
     */
    int column;
    bool visible;
} ToplevelPickerEntry;

typedef struct {
    ToplevelPickerEntry *entries;
    int entry_count;
    /**
     * The entry the keyboard navigation is on. -1 means no keyboard focus.
     * Takes priority over the mouse focus.
     */
    int keyboard_focus_idx;
    /** The entry the mouse cursor is currently over. -1 means none. */
    int mouse_focus_idx;
    /**
     * The Y position left/right movement aims for, in content coordinates.
     * It is updated by every highlight change except left/right movement,
     * which moves across columns without disturbing it.
     */
    double target_y;
    /**
     * The scroll offset of the view into the content,
     * in content coordinates. Always positive (or zero).
     */
    double scroll_y;
    /** The total height of the entry grid, in content coordinates. */
    double content_height;

    RenderTextStyle label_text_style;
} ToplevelPicker;

// All other functions are in the vtable.
void toplevel_picker_destroy(PickerSurface *picker);
