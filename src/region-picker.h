#pragma once
#include "image.h"
#include "wayland/output.h"
#include "wayland/overlay-surface.h"
#include <wayland-client.h>

typedef enum { REGION_PICKER_EMPTY, REGION_PICKER_DRAGGING } RegionPickerState;
typedef enum {
    /** A region was selected and passed in the region parameter */
    REGION_PICKER_FINISH_REASON_SELECTED,
    /** The selection was cancelled (e.g. via the Escape key)  */
    REGION_PICKER_FINISH_REASON_CANCELLED,
    /**
     * The region picker was destroyed through external means, such as its
     * output disappearing
     */
    REGION_PICKER_FINISH_REASON_DESTROYED
} RegionPickerFinishReason;

struct RegionPicker;
/**
 * A function to be called when the picker is done doing stuff, and is about to
 * be destroyed. It should always call `region_picker_destroy`.
 */
typedef void (*RegionPickerFinishCallback)(
    struct RegionPicker *picker, RegionPickerFinishReason, BBox region
);

typedef struct RegionPicker {
    OverlaySurface *surface;
    RegionPickerState state;
    cairo_surface_t *background;
    cairo_pattern_t *background_pattern;

    RegionPickerFinishCallback finish_callback;
    // Note that these values are only valid when state != REGION_PICKER_EMPTY.
    // In logical coordinates
    double x1, y1;
    double x2, y2;
    // These is kept for optimization purposes
    BBox last_drawn_box;
    uint32_t last_device_width;
    uint32_t last_device_height;
} RegionPicker;

/**
 * Note that the image is _not_ owned by the RegionPicker, and needs to stay
 * alive for as long as the RegionPicker does.
 */
RegionPicker *region_picker_new(
    WrappedOutput *output,
    Image *background,
    RegionPickerFinishCallback finish_callback
);
/** Destroy the region picker. Note that this function does NOT call the
 * finish_callback. */
void region_picker_destroy(RegionPicker *picker);
