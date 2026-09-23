#pragma once
#include "picker/output.h"
#include "picker/region.h"
#include "wayland/overlay-surface.h"
#include "wayland/screen-capture.h"
#include <wayland-client.h>

typedef enum {
    PICKER_TYPE_REGION,
    PICKER_TYPE_OUTPUT,
} PickerType;

typedef enum {
    /** Selected successfully */
    PICKER_FINISH_REASON_SELECTED,
    /** The selection was cancelled (e.g. via the Escape key)  */
    PICKER_FINISH_REASON_CANCELLED,
} PickerFinishReason;

/**
 * A set of callbacks to help orchestrate the copying process.
 * The pickers are destroyed between prepare and finish.
 */
typedef struct {
    void (*finalize_prepare)();
    /** The function takes ownership of the image. */
    void (*finalize_finish)(Image *);
    void (*finalize_cancel)();
} PickerHost;

typedef struct {
    /** list of CaptureFrame */
    struct wl_list *captures;
    /** list of PickerSurface */
    struct wl_list pickers;
    const PickerHost *host;
} PickerContext;

typedef struct PickerSurface {
    PickerContext *ctx;
    OverlaySurface *surface;
    CaptureFrame *background;
    LinkBuffer *command_arena;

    PickerType type;
    RegionPicker region;
    OutputPicker output;
    struct wl_list link;
} PickerSurface;

/**
 * @param captures list of CaptureFrame
 * The picker context takes ownership of the list of captures and will free them
 * itself.
 */
void picker_context_init(
    PickerContext *ctx,
    PickerType type,
    struct wl_list *captures,
    const PickerHost *host
);

/**
 * Call this from within the picker when the screenshot is finished or
 * cancelled. The result is owned, and only matters when the reason is SELECTED.
 */
void picker_context_finish(
    PickerSurface *surface, PickerFinishReason reason, Image *result
);
