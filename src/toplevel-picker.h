#pragma once
#include "picker-common.h"
#include "wayland/label-surface.h"
#include "wayland/overlay-surface.h"

struct ToplevelPicker;

typedef struct {
    Image *image;

    SharedBuffer *thumbnail_buf;
    struct wl_surface *thumbnail_surface;
    struct wl_subsurface *thumbnail_subsurface;
    LabelSurface *label;

    struct wl_list link;
} ToplevelPickerEntry;

// TODO: this needs to also pass the chosen toplevel
typedef void (*ToplevelPickerFinishCallback)(
    struct ToplevelPicker *picker, PickerFinishReason reason, Image *result
);

typedef struct ToplevelPicker {
    OverlaySurface *surface;
    SharedBuffer *background_buf;

    ToplevelPickerFinishCallback finish_callback;

    /** of ToplevelPickerEntry */
    struct wl_list entries;
} ToplevelPicker;

ToplevelPicker *
toplevel_picker_new(ToplevelPickerFinishCallback finish_callback);
/** Add a toplevel to the picker. */
void toplevel_picker_add(
    ToplevelPicker *picker, const char *title, Image *image
);
/** Call this when you are finished adding toplevels. */
void toplevel_picker_present(ToplevelPicker *picker);
void toplevel_picker_destroy(ToplevelPicker *picker);
