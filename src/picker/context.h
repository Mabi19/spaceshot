#pragma once
#include "picker/common.h"
#include "picker/output.h"
#include "picker/region.h"
#include "picker/smart-border.h"
#include "wayland/overlay-surface.h"
#include "wayland/screen-capture.h"
#include <wayland-client.h>

typedef struct {
    /** list of CaptureEntry */
    struct wl_list *captures;
    /** list of PickerSurface */
    struct wl_list pickers;
} PickerContext;

typedef struct PickerSurface {
    PickerContext *ctx;
    OverlaySurface *surface;
    CaptureFrame *background;
    LinkBuffer *command_arena;
    SmartBorderContext *smart_border;

    PickerType type;
    union {
        RegionPicker region;
        OutputPicker output;
    };
    struct wl_list link;
} PickerSurface;

/**
 * @param captures list of CaptureEntry
 * The picker context takes ownership of the list of captures and will free them
 * itself.
 */
void picker_context_init(
    PickerContext *ctx, PickerType type, struct wl_list *captures
);

/**
 * Call this from within the picker when the screenshot is finished or
 * cancelled.
 */
void picker_context_finish(
    PickerSurface *surface, PickerFinishReason reason, BBox crop_box
);
