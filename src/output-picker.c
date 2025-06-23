#include "output-picker.h"
#include "log.h"
#include "picker-common.h"
#include "wayland/globals.h"
#include "wayland/overlay-surface.h"
#include "wayland/render.h"
#include "wayland/seat.h"
#include "wayland/shared-memory.h"
#include <cairo.h>
#include <cursor-shape-client.h>
#include <xkbcommon/xkbcommon-keysyms.h>

static void output_picker_render(void *user_data) {
    OutputPicker *picker = user_data;
    // if (picker->state == picker->last_drawn_state) {
    //     return;
    // }

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

    picker->last_drawn_state = picker->state;
}

static void output_picker_handle_surface_close(void *user_data) {
    OutputPicker *picker = user_data;
    picker->finish_callback(picker, PICKER_FINISH_REASON_DESTROYED);
}

static void output_picker_handle_mouse(void *data, MouseEvent event) {
    OutputPicker *picker = data;
    OutputPickerState new_state = event.focus == picker->surface->wl_surface
                                      ? OUTPUT_PICKER_ACTIVE
                                      : OUTPUT_PICKER_INACTIVE;
    if (new_state != picker->state) {
        picker->state = new_state;
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
        },
        result
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

    overlay_surface_destroy(picker->surface);
    free(picker);
}
