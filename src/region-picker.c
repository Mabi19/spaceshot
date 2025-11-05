#include "region-picker.h"
#include "bbox.h"
#include "config/config.h"
#include "cursor-shape-client.h"
#include "image.h"
#include "log.h"
#include "smart-border.h"
#include "wayland/globals.h"
#include "wayland/output.h"
#include "wayland/overlay-surface.h"
#include "wayland/render.h"
#include "wayland/seat.h"
#include <cairo.h>
#include <math.h>
#include <stdlib.h>
#include <xkbcommon/xkbcommon-keysyms.h>

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
    BBox surface_bounds = {
        .x = 0,
        .y = 0,
        .width = picker->surface->device_width,
        .height = picker->surface->device_height,
    };
    result = bbox_constrain(result, surface_bounds);
    return result;
}

static void calculate_clip_regions(
    RegionPicker *picker, const BBox *curr_sel, BBox *outer, BBox *inner
) {
    uint32_t device_width = picker->surface->device_width;
    uint32_t device_height = picker->surface->device_height;
    //* This will need to be adjusted if the picker gains an edit mode
    // (which will draw further outside the bounds)
    double border_width_pixels = config_length_to_pixels(
        config_get()->region.selection_border_width, picker->surface->scale
    );

    if (device_width != picker->last_device_width ||
        device_height != picker->last_device_height) {
        // needs full redraw
        outer->x = 0;
        outer->y = 0;
        outer->width = device_width;
        outer->height = device_height;
        return;
    }

    if (picker->state == REGION_PICKER_EMPTY) {
        // selection can't have changed
        outer->width = 0;
        outer->height = 0;
        return;
    }

    BBox *last_sel = &picker->last_drawn_box;
    if (!picker->has_last_drawn_box) {
        // no previous box = entire selection needs to be damaged
        *outer = *curr_sel;
        inner->width = 0;
        inner->height = 0;
    } else {
        double last_right = last_sel->x + last_sel->width;
        double last_bottom = last_sel->y + last_sel->height;
        double curr_right = curr_sel->x + curr_sel->width;
        double curr_bottom = curr_sel->y + curr_sel->height;

        outer->x = fmin(last_sel->x, curr_sel->x);
        outer->y = fmin(last_sel->y, curr_sel->y);
        outer->width = fmax(last_right, curr_right) - outer->x;
        outer->height = fmax(last_bottom, curr_bottom) - outer->y;

        inner->x = fmax(last_sel->x, curr_sel->x);
        inner->y = fmax(last_sel->y, curr_sel->y);
        inner->width = fmin(last_right, curr_right) - inner->x;
        inner->height = fmin(last_bottom, curr_bottom) - inner->y;
    }

    // adjust for borders
    outer->x -= border_width_pixels;
    outer->y -= border_width_pixels;
    outer->width += 2 * border_width_pixels;
    outer->height += 2 * border_width_pixels;
}

/**
 * Adjust the first corner's coordinates so that the selection stays the same
 * size, but doesn't change size upon sub-pixel moving.
 */
static void adjust_opposite_corner_for_movement(RegionPicker *picker) {
    double scale = picker->surface->scale / 120.0;
    double x1 = picker->x1 * scale;
    double y1 = picker->y1 * scale;
    double x2 = picker->x2 * scale;
    double y2 = picker->y2 * scale;
    double x_offset = x2 - floor(x2);
    double y_offset = y2 - floor(y2);
    x1 = floor(x1) + x_offset;
    y1 = floor(y1) + y_offset;
    picker->x1 = x1 / scale;
    picker->y1 = y1 / scale;
}

// Like cairo_rectangle(), but in the reverse winding order.
static void cairo_bbox_reverse(cairo_t *cr, BBox rect) {
    cairo_move_to(cr, rect.x, rect.y);
    cairo_line_to(cr, rect.x, rect.y + rect.height);
    cairo_line_to(cr, rect.x + rect.width, rect.y + rect.height);
    cairo_line_to(cr, rect.x + rect.width, rect.y);
    cairo_close_path(cr);
}

static bool region_picker_draw(void *data, cairo_t *cr) {
    RegionPicker *picker = data;
    OverlaySurface *surface = picker->surface;
    BBox selection_box = get_bbox_containing_selection(picker);
    if (surface->device_width == picker->last_device_width &&
        surface->device_height == picker->last_device_height &&
        bbox_equal(selection_box, picker->last_drawn_box)) {
        log_debug("skipping draw\n");
        return false;
    }

    BBox inner_clip_region = {0}, outer_clip_region = {0};
    calculate_clip_regions(
        picker, &selection_box, &outer_clip_region, &inner_clip_region
    );

#ifndef SPACESHOT_DEBUG_CLIPPING
    cairo_reset_clip(cr);
    if (outer_clip_region.width != 0 && outer_clip_region.height != 0) {
        // apply the clip regions
        cairo_rectangle(
            cr,
            outer_clip_region.x,
            outer_clip_region.y,
            outer_clip_region.width,
            outer_clip_region.height
        );

        if (inner_clip_region.width != 0 && inner_clip_region.height != 0) {
            cairo_bbox_reverse(cr, inner_clip_region);
        }

        cairo_clip(cr);
    }
#endif

    picker->last_drawn_box = selection_box;
    picker->has_last_drawn_box = true;
    picker->last_device_width = surface->device_width;
    picker->last_device_height = surface->device_height;

    TIMING_START(frame);

    cairo_set_source(cr, picker->background_pattern);
    cairo_paint(cr);

    double x_scale =
        (double)surface->device_width /
        (double)cairo_image_surface_get_width(picker->background_surface);
    double y_scale =
        (double)surface->device_height /
        (double)cairo_image_surface_get_height(picker->background_surface);

    cairo_save(cr);
    if (x_scale != 1.0 || y_scale != 1.0) {
        log_debug("background requires scaling\n");
        cairo_scale(cr, x_scale, y_scale);
    }
    cairo_pattern_set_filter(picker->background_pattern, CAIRO_FILTER_FAST);
    cairo_set_source(cr, picker->background_pattern);
    cairo_paint(cr);
    cairo_restore(cr);

    // background
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);
    cairo_set_source_config_color(
        cr, config_get()->region.background, surface->pixel_format
    );
    cairo_rectangle(
        cr, 0.0, 0.0, surface->device_width, surface->device_height
    );

    if (picker->state != REGION_PICKER_EMPTY && selection_box.width != 0 &&
        selection_box.height != 0) {
        // poke a hole in it
        cairo_bbox_reverse(cr, selection_box);
    }
    cairo_fill(cr);

    if (picker->state != REGION_PICKER_EMPTY &&
        !(picker->x1 == picker->x2 && picker->y1 == picker->y2)) {
        // border
        // the offset is so that it doesn't occlude the visible area
        double border_width_pixels = config_length_to_pixels(
            config_get()->region.selection_border_width, surface->scale
        );
        double border_offset = border_width_pixels / 2;
        cairo_set_line_width(cr, border_width_pixels);
        if (config_get()->region.selection_border_color.type ==
            CONFIG_REGION_SELECTION_BORDER_COLOR_SMART) {
            if (picker->smart_border &&
                atomic_load_explicit(
                    &picker->smart_border->is_done, memory_order_acquire
                )) {
                cairo_set_source(cr, picker->smart_border->pattern);
            } else {
                // fallback
                // TODO: what should the fallback be?
                // probably pure white
                cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
            }
        } else {
            cairo_set_source_config_color(
                cr,
                config_get()->region.selection_border_color.v_color,
                surface->pixel_format
            );
        }
        cairo_rectangle(
            cr,
            selection_box.x - border_offset,
            selection_box.y - border_offset,
            selection_box.width + 2 * border_offset,
            selection_box.height + 2 * border_offset
        );
        cairo_stroke(cr);
    }

    TIMING_END(frame);

#ifdef SPACESHOT_DEBUG_CLIPPING
    if (outer_clip_region.width != 0 && outer_clip_region.height != 0) {
        // draw the clip regions
        cairo_rectangle(
            cr,
            outer_clip_region.x,
            outer_clip_region.y,
            outer_clip_region.width,
            outer_clip_region.height
        );

        if (inner_clip_region.width != 0 && inner_clip_region.height != 0) {
            cairo_bbox_reverse(cr, inner_clip_region);
        }

        cairo_set_source_rgba(cr, 0.0, 0.5, 0.0, 0.5);
        cairo_fill(cr);
    }
#endif

    overlay_surface_damage(surface, outer_clip_region);
    return true;
}

static void region_picker_handle_mouse(void *data, MouseEvent event) {
    RegionPicker *picker = data;

    // constrain the selection into the bounds of the picker
    double surface_x =
        fmax(0.0, fmin(event.surface_x, picker->surface->logical_width));
    double surface_y =
        fmax(0.0, fmin(event.surface_y, picker->surface->logical_height));

    // TODO: rethink a lot of this, to be more mindful of what the current state
    // is.
    // Also, the edit state needs to keep track of hovers as well.
    // So I think an outer branch that switches based on state makes the most
    // sense here.

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
            picker->has_last_drawn_box = false;
        }
    } else if (event.buttons_held & POINTER_BUTTON_LEFT) {
        if (picker->surface->wl_surface == event.focus) {
            if (picker->move_flag) {
                double dx = surface_x - picker->x2;
                double dy = surface_y - picker->y2;
                picker->x1 += dx;
                picker->y1 += dy;
                picker->x2 += dx;
                picker->y2 += dy;
            } else {
                picker->x2 = surface_x;
                picker->y2 = surface_y;
            }
        }
    }

    if (event.buttons_released & POINTER_BUTTON_LEFT &&
        picker->surface->wl_surface == event.focus &&
        picker->state == REGION_PICKER_DRAGGING) {
        if (picker->edit_flag) {
            picker->state = REGION_PICKER_EDITING;
        } else {
            BBox result_box = get_bbox_containing_selection(picker);
            double selected_region_area = result_box.width * result_box.height;
            log_debug(
                "area: %f; %f %f %f %f\n",
                selected_region_area,
                picker->x1,
                picker->y1,
                picker->x2,
                picker->y2
            );
            PickerFinishReason reason = selected_region_area > CANCEL_THRESHOLD
                                            ? PICKER_FINISH_REASON_SELECTED
                                            : PICKER_FINISH_REASON_CANCELLED;

            picker->finish_callback(picker, reason, result_box);
            return;
        }
    }

    overlay_surface_queue_draw(picker->surface);
}

static void region_picker_handle_keyboard(void *data, KeyboardEvent event) {
    RegionPicker *picker = data;
    switch (event.keysym) {
    case XKB_KEY_Escape:
        // only cancel once, on the focused surface
        if (event.type == KEYBOARD_EVENT_RELEASE &&
            picker->surface->wl_surface == event.focus) {
            picker->finish_callback(
                picker, PICKER_FINISH_REASON_CANCELLED, (BBox){}
            );
        }
        break;
    case XKB_KEY_space:
    case XKB_KEY_Alt_L:
        // moving the selection only makes sense if a selection exists
        if (picker->state == REGION_PICKER_DRAGGING) {
            picker->move_flag =
                event.type == KEYBOARD_EVENT_PRESS ? true : false;

            if (event.type == KEYBOARD_EVENT_PRESS) {
                adjust_opposite_corner_for_movement(picker);
            }

            seat_dispatcher_set_cursor_for_surface(
                wayland_globals.seat_dispatcher,
                picker->surface,
                event.type == KEYBOARD_EVENT_PRESS
                    ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING
                    : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR
            );
        }
        break;
    case XKB_KEY_Control_L:
        // keep track of ctrl key state
        // so that it can be used when released
        picker->edit_flag = event.type == KEYBOARD_EVENT_PRESS ? true : false;
    }
    // TODO: Hold Shift to lock aspect ratio
}

static SeatListener region_picker_seat_listener = {
    .mouse = region_picker_handle_mouse,
    .keyboard = region_picker_handle_keyboard
};

static void region_picker_handle_surface_close(void *data) {
    RegionPicker *picker = data;
    picker->finish_callback(picker, PICKER_FINISH_REASON_DESTROYED, (BBox){});
}

static void region_picker_handle_scale(void *data, uint32_t scale) {
    RegionPicker *picker = data;
    if (!picker->smart_border &&
        config_get()->region.selection_border_color.type ==
            CONFIG_REGION_SELECTION_BORDER_COLOR_SMART) {
        picker->smart_border =
            smart_border_context_start(picker->background_image, scale);
    }
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
        (OverlaySurfaceHandlers){
            .draw = region_picker_draw,
            .manual_render = NULL,
            .close = region_picker_handle_surface_close,
            .scale = region_picker_handle_scale,
        },
        result
    );
    result->state = REGION_PICKER_EMPTY;
    result->background_image = background;
    result->background_surface = image_make_cairo_surface(background);
    result->background_pattern =
        cairo_pattern_create_for_surface(result->background_surface);

    seat_dispatcher_add_listener(
        wayland_globals.seat_dispatcher,
        result->surface,
        &region_picker_seat_listener,
        result
    );

    seat_dispatcher_set_cursor_for_surface(
        wayland_globals.seat_dispatcher,
        result->surface,
        WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR
    );

    result->finish_callback = finish_callback;

    return result;
}

void region_picker_destroy(RegionPicker *picker) {
    log_debug("destroying region picker %p\n", (void *)picker);

    seat_dispatcher_remove_listener(
        wayland_globals.seat_dispatcher, picker->surface
    );

    if (picker->smart_border) {
        smart_border_context_unref(picker->smart_border);
    }

    cairo_pattern_destroy(picker->background_pattern);
    cairo_surface_destroy(picker->background_surface);
    overlay_surface_destroy(picker->surface);

    free(picker);
}
