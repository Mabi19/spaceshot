#include "region-picker.h"
#include "image.h"
#include "wayland/globals.h"
#include "wayland/output.h"
#include "wayland/overlay-surface.h"
#include "wayland/seat.h"
#include <cairo.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const double BORDER_WIDTH = 2.0;
// The maximum area below which a click will cancel the selection.
static const double CANCEL_THRESHOLD = 2.0;

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
    result = bbox_scale(result, picker->surface->scale / 120.0);
    result = bbox_expand_to_grid(result);
    return result;
}

static BBox region_picker_draw(void *data, cairo_t *cr) {
    RegionPicker *picker = data;
    OverlaySurface *surface = picker->surface;
    cairo_set_source(cr, picker->background_pattern);
    cairo_paint(cr);

    // background
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    const double GRAY_LEVEL = 0.05;
    // no need for eventual flip because R = B
    cairo_set_source_rgba(cr, GRAY_LEVEL, GRAY_LEVEL, GRAY_LEVEL, 0.4);
    cairo_rectangle(
        cr, 0.0, 0.0, surface->device_width, surface->device_height
    );
    BBox selection_box = get_bbox_containing_selection(picker);
    if (picker->state != REGION_PICKER_EMPTY && selection_box.width != 0 &&
        selection_box.height != 0) {
        // poke a hole in it
        cairo_rectangle(
            cr,
            selection_box.x,
            selection_box.y,
            selection_box.width,
            selection_box.height
        );
    }
    cairo_fill(cr);

    if (picker->state != REGION_PICKER_EMPTY &&
        !(picker->x1 == picker->x2 && picker->y1 == picker->y2)) {
        // border
        // the offset is so that it doesn't occlude the visible area
        double border_width_pixels =
            round((BORDER_WIDTH * surface->scale) / 120.0);
        double border_offset = border_width_pixels / 2;
        cairo_set_line_width(cr, border_width_pixels);
        // no need for eventual flip because R = B
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_rectangle(
            cr,
            selection_box.x - border_offset,
            selection_box.y - border_offset,
            selection_box.width + 2 * border_offset,
            selection_box.height + 2 * border_offset
        );
        cairo_stroke(cr);
    }

    return (BBox){
        .x = 0,
        .y = 0,
        .width = surface->device_width,
        .height = surface->device_height,
    };
}

static void region_picker_handle_mouse(void *data, MouseEvent event) {
    RegionPicker *picker = data;

    // constrain the selection into the bounds of the picker
    double surface_x =
        fmax(0.0, fmin(event.surface_x, picker->surface->logical_width));
    double surface_y =
        fmax(0.0, fmin(event.surface_y, picker->surface->logical_height));

    if (event.buttons_pressed & POINTER_BUTTON_LEFT) {
        if (picker->surface->wl_surface == event.focus) {
            picker->state = REGION_PICKER_DRAGGING;
            picker->x1 = surface_x;
            picker->y1 = surface_y;
            picker->x2 = surface_x;
            picker->y2 = surface_y;
        } else {
            // only one selection is allowed at a time
            picker->state = REGION_PICKER_EMPTY;
        }
    } else if (event.buttons_held & POINTER_BUTTON_LEFT) {
        if (picker->surface->wl_surface == event.focus) {
            picker->x2 = surface_x;
            picker->y2 = surface_y;
        }
    }

    if (event.buttons_released & POINTER_BUTTON_LEFT &&
        picker->surface->wl_surface == event.focus &&
        picker->state == REGION_PICKER_DRAGGING) {
        RegionPickerFinishCallback finish_cb = picker->finish_callback;
        double selected_region_area =
            fabs((picker->x2 - picker->x1) * (picker->y2 - picker->y1));
        printf(
            "area: %f; %f %f %f %f\n",
            selected_region_area,
            picker->x1,
            picker->y1,
            picker->x2,
            picker->y2
        );
        RegionPickerFinishReason reason =
            selected_region_area > CANCEL_THRESHOLD
                ? REGION_PICKER_FINISH_REASON_SELECTED
                : REGION_PICKER_FINISH_REASON_CANCELLED;
        BBox result_box = get_bbox_containing_selection(picker);
        region_picker_destroy(picker);
        finish_cb(picker, reason, result_box);
        return;
    }

    overlay_surface_queue_draw(picker->surface);
}

static SeatListener region_picker_seat_listener = {
    .mouse = region_picker_handle_mouse
};

static void region_picker_handle_surface_close(void *data) {
    RegionPicker *picker = data;
    RegionPickerFinishCallback finish_cb = picker->finish_callback;
    region_picker_destroy(picker);
    finish_cb(picker, REGION_PICKER_FINISH_REASON_DESTROYED, (BBox){});
}

RegionPicker *region_picker_new(
    WrappedOutput *output,
    Image *background,
    RegionPickerFinishCallback finish_callback
) {
    RegionPicker *result = calloc(1, sizeof(RegionPicker));
    result->surface = overlay_surface_new(
        output,
        background->format,
        region_picker_draw,
        region_picker_handle_surface_close,
        result
    );
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

    result->finish_callback = finish_callback;

    return result;
}

void region_picker_destroy(RegionPicker *picker) {
    printf("destroying region picker %p\n", (void *)picker);

    seat_dispatcher_remove_listener(
        wayland_globals.seat_dispatcher, picker->surface
    );

    cairo_pattern_destroy(picker->background_pattern);
    cairo_surface_destroy(picker->background);
    overlay_surface_destroy(picker->surface);

    free(picker);
}
