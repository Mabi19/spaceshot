#pragma once
#include "picker/output.h"
#include "picker/region.h"
#include "picker/toplevel.h"
#include "wayland/overlay-surface.h"
#include "wayland/screen-capture.h"
#include "wayland/seat.h"
#include <wayland-client.h>

typedef enum {
    PICKER_TYPE_REGION,
    PICKER_TYPE_OUTPUT,
    PICKER_TYPE_TOPLEVEL,
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
    /** The function takes ownership of the image. */
    void (*finalize)(Image *);
    void (*cancel)();
} PickerHost;

typedef struct {
    /** list of CaptureFrame */
    struct wl_list *captures;
    /** list of PickerSurface */
    struct wl_list pickers;
    const PickerHost *host;
} PickerContext;

/**
 * The interface each picker implements.
 * Any handler except draw may be NULL if the picker has no use for it.
 */
typedef struct PickerVTable {
    /**
     * If true, one picker is created per captured output.
     * If false, a single picker is created for the whole context
     * (with no output)
     */
    bool per_output;
    /** Called right after the picker's surface is created. */
    void (*init)(PickerSurface *picker);
    /** Called every time the picker is switched to. */
    void (*enter)(PickerSurface *picker);
    void (*draw)(PickerSurface *picker, RenderDisplayList *dl);
    void (*mouse)(PickerSurface *picker, MouseEvent event);
    void (*keyboard)(PickerSurface *picker, KeyboardEvent event);
    void (*resize)(PickerSurface *picker);
    void (*scale)(PickerSurface *picker, uint32_t scale);
} PickerVTable;

extern const PickerVTable region_picker_vtable;
extern const PickerVTable output_picker_vtable;
extern const PickerVTable toplevel_picker_vtable;

typedef struct PickerSurface {
    PickerContext *ctx;
    OverlaySurface *surface;
    CaptureFrame *background;
    LinkBuffer *command_arena;

    const PickerVTable *vtable;
    RegionPicker region;
    OutputPicker output;
    ToplevelPicker toplevel;
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
