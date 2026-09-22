#pragma once
#include "picker/common.h"
#include "picker/output.h"
#include "picker/region.h"
#include "picker/smart-border.h"
#include "wayland/output.h"
#include "wayland/overlay-surface.h"
#include "wayland/screen-capture.h"
#include "wayland/toplevel.h"
#include <wayland-client.h>

/** The type of image this entry stores. */
typedef enum {
    CAPTURE_ENTRY_TYPE_OUTPUT,
    CAPTURE_ENTRY_TYPE_TOPLEVEL,
} CaptureEntryType;

typedef struct {
    CaptureEntryType frame_type;
    union {
        WrappedOutput *output;
        WrappedToplevel *toplevel;
    };
    CaptureFrame *frame;
    struct wl_list link;
} CaptureEntry;

void capture_entry_destroy(CaptureEntry *entry);
void capture_entry_destroy_all(struct wl_list *captures);

typedef struct PickerSurface {
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

typedef struct {
    /** list of CaptureEntry */
    struct wl_list *captures;
    /** list of PickerSurface */
    struct wl_list surfaces;
} PickerContext;

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
void picker_context_finish(PickerSurface *surface, PickerFinishReason reason);
