#include "picker/region-picker.h"
#include "image.h"
#include "wayland/globals.h"
#include "wayland/output.h"
#include "wayland/overlay-surface.h"
#include "wayland/seat.h"
#include <cairo.h>
#include <stdlib.h>

static BBox get_bbox_containing_selection(RegionPicker *picker) {
    double left = fmin(picker->x1, picker->x2);
    double top = fmin(picker->y1, picker->y2);
    double right = fmax(picker->x1, picker->x2);
    double bottom = fmax(picker->y1, picker->y2);

    BBox result = {
        .x = left,
        .y = top,
        .width = right - left,
        .height = bottom - top,
    };
    BBox scaled = bbox_scale(&result, picker->surface->scale / 120.0);
    BBox rounded = bbox_round(&scaled);
    return rounded;
}

static BBox region_picker_draw(void *data, cairo_t *cr) {
    RegionPicker *picker = data;
    cairo_set_source(cr, picker->background_pattern);
    cairo_paint(cr);

    BBox selection_box = get_bbox_containing_selection(picker);
    if (selection_box.width != 0 && selection_box.height != 0) {
        cairo_rectangle(
            cr,
            selection_box.x,
            selection_box.y,
            selection_box.width,
            selection_box.height
        );
        cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
        cairo_fill(cr);
    }

    return (BBox){
        .x = 0,
        .y = 0,
        .width = picker->surface->device_width,
        .height = picker->surface->device_height,
    };
}

static void region_picker_handle_mouse(void *data, MouseEvent event) {
    // TODO: Constrain the selection into the bounds of the picker.

    RegionPicker *picker = data;
    if (event.buttons_pressed & POINTER_BUTTON_LEFT) {
        if (picker->surface->wl_surface == event.focus) {
            picker->state = REGION_PICKER_DRAGGING;
            picker->x1 = event.surface_x;
            picker->y1 = event.surface_y;
            picker->x2 = event.surface_x;
            picker->y2 = event.surface_y;
        } else {
            // only one selection is allowed at a time
            picker->state = REGION_PICKER_EMPTY;
        }
    } else if (event.buttons_held & POINTER_BUTTON_LEFT) {
        if (picker->surface->wl_surface == event.focus) {
            picker->x2 = event.surface_x;
            picker->y2 = event.surface_y;
        }
    }

    overlay_surface_queue_draw(picker->surface);
}

static SeatListener region_picker_seat_listener = {
    .mouse = region_picker_handle_mouse
};

RegionPicker *region_picker_new(WrappedOutput *output, Image *background) {
    RegionPicker *result = calloc(1, sizeof(RegionPicker));
    result->surface = overlay_surface_new(output, region_picker_draw, result);
    result->state = REGION_PICKER_EMPTY;
    result->background = image_make_cairo_surface(background);
    result->background_pattern =
        cairo_pattern_create_for_surface(result->background);

    seat_dispatcher_add_listener(
        wayland_globals.seat_dispatcher,
        result->surface,
        &region_picker_seat_listener,
        result
    );

    return result;
}
