#include "region-picker.h"
#include "anchor.h"
#include "bbox.h"
#include "image.h"
#include "link-buffer.h"
#include "log.h"
#include "render/command.h"
#include "render/texture.h"
#include "smart-border.h"
#include "wayland/globals.h"
#include "wayland/output.h"
#include "wayland/overlay-surface.h"
#include "wayland/seat.h"
#include <config/config.h>
#include <cursor-shape-client.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>

// The maximum area below which a click will cancel the selection.
static const double CANCEL_THRESHOLD = 2.0;

static BBox get_bbox_containing_selection(RegionPicker *picker) {
    if (picker->state == REGION_PICKER_EMPTY) {
        return (BBox){.x = 0, .y = 0, .width = 0, .height = 0};
    }

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

static bool
hit_test_at_position(RegionPicker *picker, double x, double y, Anchor *anchor) {
    const double NEAR_THRESHOLD = 12;

    double left = fmax(fmin(picker->x1, picker->x2), 0);
    double right =
        fmin(fmax(picker->x1, picker->x2), picker->surface->logical_width);
    double top = fmax(fmin(picker->y1, picker->y2), 0);
    double bottom =
        fmin(fmax(picker->y1, picker->y2), picker->surface->logical_height);

    double dist_left = fabs(left - x);
    double dist_right = fabs(right - x);
    double dist_top = fabs(top - y);
    double dist_bottom = fabs(bottom - y);

    if (x < left - NEAR_THRESHOLD || x > right + NEAR_THRESHOLD ||
        y < top - NEAR_THRESHOLD || y > bottom + NEAR_THRESHOLD) {
        return false;
    }

    Anchor result = ANCHOR_CENTER;
    if (dist_left < dist_right) {
        if (dist_left < NEAR_THRESHOLD) {
            result |= ANCHOR_LEFT;
        }
    } else {
        if (dist_right < NEAR_THRESHOLD) {
            result |= ANCHOR_RIGHT;
        }
    }

    if (dist_top < dist_bottom) {
        if (dist_top < NEAR_THRESHOLD) {
            result |= ANCHOR_TOP;
        }
    } else {
        if (dist_bottom < NEAR_THRESHOLD) {
            result |= ANCHOR_BOTTOM;
        }
    }

    *anchor = result;
    return true;
}

static enum wp_cursor_shape_device_v1_shape
get_cursor_for_anchor(Anchor anchor) {
    switch (anchor) {
    case ANCHOR_CENTER:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRAB;
    case ANCHOR_TOP:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE;
    case ANCHOR_BOTTOM:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE;
    case ANCHOR_LEFT:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE;
    case ANCHOR_RIGHT:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE;
    case ANCHOR_TOP_LEFT:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE;
    case ANCHOR_TOP_RIGHT:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE;
    case ANCHOR_BOTTOM_LEFT:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE;
    case ANCHOR_BOTTOM_RIGHT:
        return WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE;
    default:
        REPORT_UNHANDLED("anchor", "%d", anchor);
    }
}

/**
 * Decompose a box and a hole within it into (up to) 4 normal boxes.
 * Returns the amount of boxes created from the decomposition (filled slots in
 * the array)
 */
static int decompose_holey_bbox(BBox outer, BBox inner, BBox out[4]) {
    int i = 0;
    BBox current_box;
    // top
    current_box = (BBox){
        .x = outer.x,
        .y = outer.y,
        .width = outer.width,
        .height = inner.y - outer.y
    };
    if (current_box.width > 0 && current_box.height > 0) {
        out[i] = current_box;
        i++;
    }
    // bottom
    current_box = (BBox){
        .x = outer.x,
        .y = inner.y + inner.height,
        .width = outer.width,
        .height = outer.y + outer.height - inner.y - inner.height
    };
    if (current_box.width > 0 && current_box.height > 0) {
        out[i] = current_box;
        i++;
    }
    // left
    current_box = (BBox){
        .x = outer.x,
        .y = inner.y,
        .width = inner.x - outer.x,
        .height = inner.height,
    };
    if (current_box.width > 0 && current_box.height > 0) {
        out[i] = current_box;
        i++;
    }
    // right
    current_box = (BBox){
        .x = inner.x + inner.width,
        .y = inner.y,
        .width = outer.x + outer.width - inner.x - inner.width,
        .height = inner.height,
    };
    if (current_box.width > 0 && current_box.height > 0) {
        out[i] = current_box;
        i++;
    }
    return i;
}

static RenderDisplayList region_picker_draw(void *data) {
    RegionPicker *picker = data;
    OverlaySurface *surface = picker->surface;

    link_buffer_reset(picker->command_arena);
    RenderDisplayList dl = {.arena = picker->command_arena};

    // The full surface.
    BBox full_surface_box = {
        0, 0, surface->device_width, surface->device_height
    };
    // The inside of the selection (also the inner edge of the border).
    BBox selection_box = get_bbox_containing_selection(picker);
    // The outer edge of the selection border.
    BBox border_box = selection_box;
    double border_width_pixels = config_length_to_pixels(
        config_get()->region.selection_border_width, surface->scale
    );
    border_box.x -= border_width_pixels;
    border_box.y -= border_width_pixels;
    border_box.width += 2.0 * border_width_pixels;
    border_box.height += 2.0 * border_width_pixels;

    RENDER_RECT(
        dl,
        .bounds = full_surface_box,
        .color = RENDER_COLOR_DEFAULT,
        .texture = picker->background_texture,
        .uv = RENDER_UV_DEFAULT,
    );

    // dark overlay
    if (selection_box.width > 0 && selection_box.height > 0) {
        BBox rects[4];
        int count = decompose_holey_bbox(full_surface_box, border_box, rects);
        for (int i = 0; i < count; i++) {
            RENDER_RECT(
                dl,
                .bounds = rects[i],
                .color = config_color_to_render_color(
                    config_get()->region.background
                ),
            );
        }
    } else {
        RENDER_RECT(
            dl,
            .bounds = full_surface_box,
            .color =
                config_color_to_render_color(config_get()->region.background),
        );
    }

    if (picker->state != REGION_PICKER_EMPTY &&
        !(picker->x1 == picker->x2 && picker->y1 == picker->y2)) {
        RenderColor border_color;
        RenderTexture *border_texture = NULL;
        if (config_get()->region.selection_border_color.type ==
            CONFIG_REGION_SELECTION_BORDER_COLOR_SMART) {
            if (picker->smart_border &&
                atomic_load_explicit(
                    &picker->smart_border->is_done, memory_order_acquire
                )) {
                border_color = (RenderColor){1, 1, 1, 1};
                // The smart border code cannot create a texture itself, because
                // it runs off-thread.
                if (!picker->smart_border->result_texture) {
                    picker->smart_border->result_texture =
                        picker->surface->renderer->texture_new_from_image(
                            picker->smart_border->result_image
                        );
                }
                border_texture = picker->smart_border->result_texture;
            } else {
                // fallback
                border_color = (RenderColor){1, 1, 1, 1};
            }
        } else {
            border_color = config_color_to_render_color(
                config_get()->region.selection_border_color.v_color
            );
        }

        BBox border_rects[4];
        int count =
            decompose_holey_bbox(border_box, selection_box, border_rects);

        for (int i = 0; i < count; i++) {
            BBox border_rect = border_rects[i];

            RenderUV screenspace_uv = {
                border_rect.x / surface->device_width,
                border_rect.y / surface->device_height,
                (border_rect.x + border_rect.width) / surface->device_width,
                (border_rect.y + border_rect.height) / surface->device_height,
            };

            RENDER_RECT(
                dl,
                .bounds = border_rect,
                .color = border_color,
                .texture = border_texture,
                .uv = screenspace_uv,
            );
        }

        if (picker->state == REGION_PICKER_EDITING) {
            double border_center_offset = border_width_pixels / 2.0;
            double x_positions[] = {
                selection_box.x - border_center_offset,
                selection_box.x + selection_box.width / 2.0,
                selection_box.x + selection_box.width + border_center_offset,
                selection_box.x + selection_box.width + border_center_offset,
                selection_box.x + selection_box.width + border_center_offset,
                selection_box.x + selection_box.width / 2.0,
                selection_box.x - border_center_offset,
                selection_box.x - border_center_offset,
            };
            double y_positions[] = {
                selection_box.y - border_center_offset,
                selection_box.y - border_center_offset,
                selection_box.y - border_center_offset,
                selection_box.y + selection_box.height / 2.0,
                selection_box.y + selection_box.height + border_center_offset,
                selection_box.y + selection_box.height + border_center_offset,
                selection_box.y + selection_box.height + border_center_offset,
                selection_box.y + selection_box.height / 2.0,
            };

            double inner_half_size =
                border_width_pixels / 2.0 + 2.0 * surface->scale / 120.0;
            double outer_half_size =
                inner_half_size + 2.0 * surface->scale / 120.0;

            bool is_smart = config_get()->region.selection_border_color.type ==
                            CONFIG_REGION_SELECTION_BORDER_COLOR_SMART;
            RenderColor outer_handle_color;
            if (is_smart) {
                outer_handle_color = (RenderColor){1.0, 1.0, 1.0, 1.0};
            } else {
                outer_handle_color = border_color;
            }
            for (int i = 0; i < 8; i++) {
                double x = x_positions[i];
                double y = y_positions[i];
                RENDER_RECT(
                    dl,
                    .bounds =
                        (BBox){
                            x - outer_half_size,
                            y - outer_half_size,
                            2.0 * outer_half_size,
                            2.0 * outer_half_size
                        },
                    .color = outer_handle_color
                );
            }

            RenderColor inner_handle_color;
            if (is_smart) {
                inner_handle_color = (RenderColor){0.0, 0.0, 0.0, 1.0};
            } else {
                ConfigColor border =
                    config_get()->region.selection_border_color.v_color;
                float gray_level = (border.r + border.g + border.b) / 3.0;
                if (gray_level < 0.4375) {
                    inner_handle_color = (RenderColor){1.0, 1.0, 1.0, 1.0};
                } else {
                    inner_handle_color = (RenderColor){0.0, 0.0, 0.0, 1.0};
                }
            }
            for (int i = 0; i < 8; i++) {
                double x = x_positions[i];
                double y = y_positions[i];
                RENDER_RECT(
                    dl,
                    .bounds =
                        (BBox){
                            x - inner_half_size,
                            y - inner_half_size,
                            2.0 * inner_half_size,
                            2.0 * inner_half_size
                        },
                    .color = inner_handle_color,
                );
            }
        }
    }

    return dl;
}

static void update_cursor_shape(RegionPicker *picker) {
    enum wp_cursor_shape_device_v1_shape shape;
    switch (picker->state) {
    case REGION_PICKER_EMPTY:
        shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR;
        break;
    case REGION_PICKER_DRAGGING:
        shape = picker->move_flag ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING
                                  : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR;
        break;
    case REGION_PICKER_EDITING:
        if (wayland_globals.seat_dispatcher->pointer_data.focus ==
            picker->surface->wl_surface) {
            if (picker->edit_data.is_move) {
                shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_GRABBING;
            } else {
                Anchor anchor;
                if (hit_test_at_position(
                        picker,
                        wayland_globals.seat_dispatcher->pointer_data.surface_x,
                        wayland_globals.seat_dispatcher->pointer_data.surface_y,
                        &anchor
                    )) {
                    shape = get_cursor_for_anchor(anchor);
                } else {
                    shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
                }
            }
        } else {
            shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
        }
        break;
    default:
        REPORT_UNHANDLED("region picker state", "%d", picker->state);
    }
    seat_dispatcher_set_cursor_for_surface(
        wayland_globals.seat_dispatcher, picker->surface, shape
    );
}

static void change_state(RegionPicker *picker, RegionPickerState new_state) {
    switch (new_state) {
    case REGION_PICKER_EMPTY:
        break;
    case REGION_PICKER_DRAGGING:
        picker->move_flag = false;
        break;
    case REGION_PICKER_EDITING:
        memset(&picker->edit_data, 0, sizeof(picker->edit_data));
        break;
    }
    picker->state = new_state;
    update_cursor_shape(picker);
}

static void confirm_selection(RegionPicker *picker) {
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
}

static void region_picker_handle_mouse(void *data, MouseEvent event) {
    RegionPicker *picker = data;

    RegionPickerState prev_state = picker->state;
    double prev_x1 = picker->x1;
    double prev_y1 = picker->y1;
    double prev_x2 = picker->x2;
    double prev_y2 = picker->y2;

    // constrain the selection into the bounds of the picker
    double surface_x =
        fmax(0.0, fmin(event.surface_x, picker->surface->logical_width));
    double surface_y =
        fmax(0.0, fmin(event.surface_y, picker->surface->logical_height));

    switch (picker->state) {
    case REGION_PICKER_EMPTY: {
        if (event.buttons_pressed & POINTER_BUTTON_LEFT &&
            picker->surface->wl_surface == event.focus) {
            picker->x1 = surface_x;
            picker->y1 = surface_y;
            picker->x2 = surface_x;
            picker->y2 = surface_y;
            change_state(picker, REGION_PICKER_DRAGGING);
        }
        break;
    }
    case REGION_PICKER_DRAGGING: {
        if (event.buttons_released & POINTER_BUTTON_LEFT) {
            if (picker->edit_flag) {
                change_state(picker, REGION_PICKER_EDITING);
            } else {
                confirm_selection(picker);
                return;
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
        break;
    }
    case REGION_PICKER_EDITING: {
        if (picker->edit_data.is_move) {
            // This constrains it so that the box is always fully on the screen.
            double new_x1 = surface_x - picker->edit_data.grab_offset_x;
            double new_y1 = surface_y - picker->edit_data.grab_offset_y;
            if (picker->x1 < picker->x2) {
                new_x1 = fmin(
                    fmax(new_x1, 0),
                    picker->surface->logical_width - (picker->x2 - picker->x1)
                );
            } else {
                new_x1 = fmax(
                    fmin(new_x1, picker->surface->logical_width),
                    picker->x1 - picker->x2
                );
            }

            if (picker->y1 < picker->y2) {
                new_y1 = fmin(
                    fmax(new_y1, 0),
                    picker->surface->logical_height - (picker->y2 - picker->y1)
                );
            } else {
                new_y1 = fmax(
                    fmin(new_y1, picker->surface->logical_height),
                    picker->y1 - picker->y2
                );
            }

            picker->x2 += new_x1 - picker->x1;
            picker->y2 += new_y1 - picker->y1;
            picker->x1 = new_x1;
            picker->y1 = new_y1;
        } else {
            if (picker->edit_data.modify_x) {
                *picker->edit_data.modify_x =
                    surface_x - picker->edit_data.grab_offset_x;
            }
            if (picker->edit_data.modify_y) {
                *picker->edit_data.modify_y =
                    surface_y - picker->edit_data.grab_offset_y;
            }
        }

        if (event.buttons_pressed & POINTER_BUTTON_LEFT) {
            if (event.focus != picker->surface->wl_surface) {
                change_state(picker, REGION_PICKER_EMPTY);
            } else {
                Anchor anchor;
                if (hit_test_at_position(
                        picker, surface_x, surface_y, &anchor
                    )) {
                    double *left =
                        picker->x1 < picker->x2 ? &picker->x1 : &picker->x2;
                    double *right =
                        picker->x1 < picker->x2 ? &picker->x2 : &picker->x1;
                    double *top =
                        picker->y1 < picker->y2 ? &picker->y1 : &picker->y2;
                    double *bottom =
                        picker->y1 < picker->y2 ? &picker->y2 : &picker->y1;

                    if (anchor == ANCHOR_CENTER) {
                        // we're not forced into fixing any specific corner in
                        // place here, so this function also works
                        adjust_opposite_corner_for_movement(picker);

                        picker->edit_data.is_move = true;
                        picker->edit_data.grab_offset_x =
                            surface_x - picker->x1;
                        picker->edit_data.grab_offset_y =
                            surface_y - picker->y1;
                    } else {
                        picker->edit_data.is_move = false;
                        if (anchor & ANCHOR_LEFT) {
                            picker->edit_data.modify_x = left;
                            picker->edit_data.grab_offset_x = surface_x - *left;
                        } else if (anchor & ANCHOR_RIGHT) {
                            picker->edit_data.modify_x = right;
                            picker->edit_data.grab_offset_x =
                                surface_x - *right;
                        }

                        if (anchor & ANCHOR_TOP) {
                            picker->edit_data.modify_y = top;
                            picker->edit_data.grab_offset_y = surface_y - *top;
                        } else if (anchor & ANCHOR_BOTTOM) {
                            picker->edit_data.modify_y = bottom;
                            picker->edit_data.grab_offset_y =
                                surface_y - *bottom;
                        }
                    }
                } else {
                    // click outside
                    picker->x1 = surface_x;
                    picker->y1 = surface_y;
                    picker->x2 = surface_x;
                    picker->y2 = surface_y;
                    change_state(picker, REGION_PICKER_DRAGGING);
                }
            }
        } else if (event.buttons_released & POINTER_BUTTON_LEFT) {
            picker->edit_data.is_move = false;
            picker->edit_data.modify_x = NULL;
            picker->edit_data.modify_y = NULL;
        }

        update_cursor_shape(picker);
        break;
    }
    default:
        REPORT_UNHANDLED("region picker state", "%d", picker->state);
    }

    if (prev_state != picker->state || prev_x1 != picker->x1 ||
        prev_y1 != picker->y1 || prev_x2 != picker->x2 ||
        prev_y2 != picker->y2) {
        overlay_surface_queue_draw(picker->surface);
    }
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
        if (picker->state != REGION_PICKER_EMPTY) {
            picker->move_flag =
                event.type == KEYBOARD_EVENT_PRESS ? true : false;
        }
        if (picker->state == REGION_PICKER_DRAGGING) {
            if (event.type == KEYBOARD_EVENT_PRESS) {
                adjust_opposite_corner_for_movement(picker);
            }
            update_cursor_shape(picker);
        }
        break;
    case XKB_KEY_Control_L:
        // keep track of ctrl key state
        // so that it can be used when released
        picker->edit_flag = event.type == KEYBOARD_EVENT_PRESS ? true : false;
        break;
    case XKB_KEY_Return:
        // In edit mode, an explicit confirmation is necessary
        if (event.type == KEYBOARD_EVENT_RELEASE &&
            picker->state == REGION_PICKER_EDITING &&
            picker->surface->wl_surface == event.focus) {
            confirm_selection(picker);
        }
        break;
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
            .close = region_picker_handle_surface_close,
            .scale = region_picker_handle_scale,
        },
        result
    );
    result->state = REGION_PICKER_EMPTY;
    result->background_image = background;
    result->background_texture =
        result->surface->renderer->texture_new_from_image(background);

    result->command_arena = link_buffer_new(LINK_BUFFER_ARENA_SIZE);

    seat_dispatcher_add_listener(
        wayland_globals.seat_dispatcher,
        result->surface,
        &region_picker_seat_listener,
        result
    );
    update_cursor_shape(result);

    result->finish_callback = finish_callback;

    return result;
}

void region_picker_destroy(RegionPicker *picker) {
    log_debug("destroying region picker %p\n", (void *)picker);

    seat_dispatcher_remove_listener(
        wayland_globals.seat_dispatcher, picker->surface
    );

    if (picker->smart_border) {
        if (picker->smart_border->result_texture) {
            picker->surface->renderer->texture_destroy(
                picker->smart_border->result_texture
            );
        }
        smart_border_context_unref(picker->smart_border);
    }

    picker->surface->renderer->texture_destroy(picker->background_texture);
    overlay_surface_destroy(picker->surface);

    free(picker);
}
