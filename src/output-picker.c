#include "output-picker.h"
#include "bbox.h"
#include "config/config.h"
#include "log.h"
#include "picker-common.h"
#include "wayland/globals.h"
#include "wayland/label-surface.h"
#include "wayland/overlay-surface.h"
#include "wayland/render.h"
#include "wayland/seat.h"
#include "wayland/shared-memory.h"
#include <cairo.h>
#include <cursor-shape-client.h>
#include <stdlib.h>
#include <xkbcommon/xkbcommon-keysyms.h>

static const double LABEL_Y_OFFSET = 12;

static void output_picker_render(void *user_data) {
    OutputPicker *picker = user_data;

    label_surface_set_position(
        picker->label,
        picker->surface->logical_width / 2,
        picker->move_label_down
            ? picker->surface->logical_height - LABEL_Y_OFFSET
            : LABEL_Y_OFFSET,
        picker->move_label_down ? LABEL_SURFACE_ANCHOR_BOTTOM
                                : LABEL_SURFACE_ANCHOR_TOP
    );
    label_surface_show(picker->label);

    if (picker->state == picker->last_drawn_state) {
        return;
    }

    if (picker->state == OUTPUT_PICKER_ACTIVE) {
        wl_surface_attach(
            picker->surface->wl_surface, picker->background_buf->wl_buffer, 0, 0
        );
    } else {
        wl_surface_attach(
            picker->surface->wl_surface,
            picker->background_inactive_buf->wl_buffer,
            0,
            0
        );
    }
    overlay_surface_damage(
        picker->surface,
        (BBox){.x = 0, .y = 0, .width = 0xffff, .height = 0xffff}
    );

    picker->last_drawn_state = picker->state;
}

static void output_picker_handle_surface_close(void *user_data) {
    OutputPicker *picker = user_data;
    picker->finish_callback(picker, PICKER_FINISH_REASON_DESTROYED);
}

static void output_picker_handle_mouse(void *data, MouseEvent event) {
    OutputPicker *picker = data;
    bool should_redraw = false;
    OutputPickerState new_state = event.focus == picker->surface->wl_surface
                                      ? OUTPUT_PICKER_ACTIVE
                                      : OUTPUT_PICKER_INACTIVE;
    if (new_state != picker->state) {
        picker->state = new_state;
        should_redraw = true;
    }

    int32_t label_width =
        picker->label->device_width * picker->label->scale / 120.0;
    int32_t label_height =
        picker->label->device_height * picker->label->scale / 120.0;
    int32_t center_x = picker->surface->logical_width / 2;

    // intentionally a bit bigger than the label
    bool new_move = fabs(center_x - event.surface_x) < label_width / 2.0 + 24 &&
                    event.surface_y < label_height + LABEL_Y_OFFSET + 24;

    if (picker->move_label_down != new_move) {
        picker->move_label_down = new_move;
        should_redraw = true;
    }

    if (should_redraw) {
        overlay_surface_queue_draw(picker->surface);
    }

    if (picker->state == OUTPUT_PICKER_ACTIVE &&
        event.buttons_released & POINTER_BUTTON_LEFT) {
        picker->finish_callback(picker, PICKER_FINISH_REASON_SELECTED);
    }
}

static void output_picker_handle_keyboard(void *data, KeyboardEvent event) {
    OutputPicker *picker = data;
    switch (event.keysym) {
    case XKB_KEY_Escape:
        // only cancel once, on the focused surface
        if (picker->state == OUTPUT_PICKER_ACTIVE) {
            picker->finish_callback(picker, PICKER_FINISH_REASON_CANCELLED);
        }
        break;
    }
}

static SeatListener output_picker_seat_listener = {
    .mouse = output_picker_handle_mouse,
    .keyboard = output_picker_handle_keyboard,
};

OutputPicker *output_picker_new(
    WrappedOutput *output,
    Image *background,
    OutputPickerFinishCallback finish_callback
) {
    OutputPicker *result = calloc(1, sizeof(OutputPicker));
    result->surface = overlay_surface_new(
        output,
        background->format,
        (OverlaySurfaceHandlers){
            .draw = NULL,
            .manual_render = output_picker_render,
            .close = output_picker_handle_surface_close,
            .scale = NULL,
        },
        result
    );

    result->label = label_surface_new(
        result->surface->wl_surface,
        output->name,
        (LabelSurfaceStyle){
            .font_family = "Sans",
            .font_size = {.unit = CONFIG_LENGTH_UNIT_PX, .value = 16},
            .text_color = {.r = 1.0, .g = 1.0, .b = 1.0, .a = 1.0},
            .background_color = {.r = 0.0, .g = 0.0, .b = 0.0, .a = 0.75},
            .padding_x = {.unit = CONFIG_LENGTH_UNIT_PX, .value = 6.0},
            .padding_y = {.unit = CONFIG_LENGTH_UNIT_PX, .value = 4.0},
            .corner_radius = 4.0
        }
    );

    result->background = background;
    result->background_buf = shared_buffer_new(
        background->width,
        background->height,
        background->stride,
        image_format_to_wl(background->format)
    );
    memcpy(
        result->background_buf->data,
        background->data,
        background->height * background->stride
    );

    result->background_inactive_buf = shared_buffer_new(
        background->width,
        background->height,
        background->stride,
        image_format_to_wl(background->format)
    );
    memcpy(
        result->background_inactive_buf->data,
        background->data,
        background->height * background->stride
    );
    cairo_surface_t *cairo_surface = cairo_image_surface_create_for_data(
        result->background_inactive_buf->data,
        image_format_to_cairo(background->format),
        background->width,
        background->height,
        background->stride
    );
    cairo_t *cr = cairo_create(cairo_surface);
    cairo_set_source_config_color(
        cr,
        (ConfigColor){.r = 0.0625, .g = 0.0625, .b = 0.0625, .a = 0.4},
        background->format
    );
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(cairo_surface);
    cairo_surface_destroy(cairo_surface);

    seat_dispatcher_add_listener(
        wayland_globals.seat_dispatcher,
        result->surface,
        &output_picker_seat_listener,
        result
    );
    seat_dispatcher_set_cursor_for_surface(
        wayland_globals.seat_dispatcher,
        result->surface,
        WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR
    );

    result->state = OUTPUT_PICKER_INACTIVE;
    result->last_drawn_state = OUTPUT_PICKER_UNINITIALIZED;
    result->finish_callback = finish_callback;

    return result;
}

void output_picker_destroy(OutputPicker *picker) {
    log_debug("destroying output picker %p\n", (void *)picker);

    seat_dispatcher_remove_listener(
        wayland_globals.seat_dispatcher, picker->surface
    );

    shared_buffer_destroy(picker->background_buf);
    shared_buffer_destroy(picker->background_inactive_buf);

    label_surface_destroy(picker->label);
    overlay_surface_destroy(picker->surface);
    free(picker);
}
