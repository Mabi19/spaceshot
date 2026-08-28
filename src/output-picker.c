#include "output-picker.h"
#include "bbox.h"
#include "link-buffer.h"
#include "log.h"
#include "picker-common.h"
#include "render/command.h"
#include "wayland/globals.h"
#include "wayland/overlay-surface.h"
#include "wayland/seat.h"
#include <cairo.h>
#include <cursor-shape-client.h>
#include <stdlib.h>
#include <xkbcommon/xkbcommon-keysyms.h>

constexpr double LABEL_PADDING_X = 6;
constexpr double LABEL_PADDING_Y = 4;
constexpr double LABEL_Y_OFFSET = 12;

static RenderDisplayList output_picker_draw(void *data) {
    OutputPicker *picker = data;
    OverlaySurface *surface = picker->surface;
    link_buffer_reset(picker->command_arena);
    RenderDisplayList dl = {.arena = picker->command_arena};
    BBox full_surface_box =
        (BBox){0, 0, surface->device_width, surface->device_height};

    RENDER_RECT(
        dl,
        .bounds = full_surface_box,
        .color = RENDER_COLOR_DEFAULT,
        .texture = picker->background_texture,
        .uv = RENDER_UV_DEFAULT
    );

    if (picker->state != OUTPUT_PICKER_ACTIVE) {
        // Pretty close to the default region picker background color.
        // TODO: Rework how style configuration is done (or drop it entirely?)
        RENDER_RECT(
            dl, .bounds = full_surface_box, .color = {0.025, 0.025, 0.025, 0.4}
        );
    }

    // TODO: The label here could probably be drawn a lot easier with Clay.

    double padding_x = LABEL_PADDING_X * surface->scale / 120.0;
    double padding_y = LABEL_PADDING_Y * surface->scale / 120.0;
    double y_offset = LABEL_Y_OFFSET * surface->scale / 120.0;
    RenderTextMetrics label_size = picker->label_size;
    double text_x = (surface->device_width - label_size.width) / 2.0;
    double text_y = picker->move_label_down
                        ? (surface->device_height - y_offset - padding_y -
                           label_size.height)
                        : (y_offset + padding_y);

    RENDER_RECT(
        dl,
        .bounds =
            (BBox){
                .x = text_x - padding_x,
                .y = text_y - padding_y,
                .width = label_size.width + 2 * padding_x,
                .height = label_size.height + 2 * padding_y,
            },
        .color = {0, 0, 0, 0.75},
        .border_radius = RENDER_BORDER_RADIUS(4)
    );
    RenderTextStyle scaled_style = RENDER_TEXT_STYLE_DEFAULT(surface->scale);
    RENDER_TEXT(
        dl,
        .x = text_x,
        .y = text_y,
        picker->output_name,
        -1,
        scaled_style,
        .color = RENDER_COLOR_DEFAULT
    );

    return dl;
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

    // intentionally a bit bigger than the label
    int32_t center_x = picker->surface->logical_width / 2;
    double logical_label_w =
        picker->label_size.width / picker->surface->scale * 120.0;
    double logical_label_h =
        picker->label_size.height / picker->surface->scale * 120.0;
    bool new_move =
        fabs(center_x - event.surface_x) < logical_label_w / 2.0 + 24 &&
        (event.surface_y) < logical_label_h + LABEL_Y_OFFSET + 24;

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

static void output_picker_recalculate_label_size(void *data, uint32_t scale) {
    OutputPicker *picker = data;
    RenderTextStyle scaled_style = RENDER_TEXT_STYLE_DEFAULT(scale);
    picker->label_size = picker->surface->renderer->measure_text(
        picker->output_name, -1, scaled_style
    );
}

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
            .draw = output_picker_draw,
            .close = output_picker_handle_surface_close,
            .scale = output_picker_recalculate_label_size,
        },
        result
    );

    result->output_name = output->name;
    output_picker_recalculate_label_size(result, 120);

    result->background_image = background;
    result->background_texture =
        result->surface->renderer->texture_new_from_image(background);
    result->command_arena = link_buffer_new(LINK_BUFFER_ARENA_SIZE);

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
    result->finish_callback = finish_callback;

    return result;
}

void output_picker_destroy(OutputPicker *picker) {
    log_debug("destroying output picker %p\n", (void *)picker);

    seat_dispatcher_remove_listener(
        wayland_globals.seat_dispatcher, picker->surface
    );
    picker->surface->renderer->texture_destroy(picker->background_texture);
    link_buffer_destroy(picker->command_arena);

    overlay_surface_destroy(picker->surface);
    free(picker);
}
