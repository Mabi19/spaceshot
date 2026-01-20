#pragma once
#include "image.h"
#include "picker-common.h"
#include "smart-border.h"
#include "wayland/output.h"
#include "wayland/overlay-surface.h"
#include <threads.h>
#include <wayland-client.h>

typedef enum {
    REGION_PICKER_EMPTY,
    REGION_PICKER_DRAGGING,
    REGION_PICKER_EDITING
} RegionPickerState;

struct RegionPicker;
/**
 * A function to be called when the picker is done doing stuff, and is about to
 * be destroyed. It should always call `region_picker_destroy`.
 */
typedef void (*RegionPickerFinishCallback)(
    struct RegionPicker *picker, PickerFinishReason reason, BBox region
);

typedef struct RegionPicker {
    OverlaySurface *surface;
    RegionPickerState state;
    const Image *background_image;
    cairo_surface_t *background_surface;
    cairo_pattern_t *background_pattern;
    SmartBorderContext *smart_border;

    RegionPickerFinishCallback finish_callback;
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
    // These are kept for optimization purposes
    bool dirty_after_state_change;
    // This flag needs to be unset every time the selection is cleared.
    bool can_compare_boxes;
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
