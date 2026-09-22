#include "output.h"
#include "bbox.h"
#include "picker/common.h"
#include "picker/context.h"
#include "render/command.h"
#include "wayland/globals.h"
#include "wayland/overlay-surface.h"
#include "wayland/seat.h"
#include <cursor-shape-client.h>
#include <stdlib.h>
#include <string.h>
#include <xkbcommon/xkbcommon-keysyms.h>

constexpr double LABEL_PADDING_X = 6;
constexpr double LABEL_PADDING_Y = 4;
constexpr double LABEL_Y_OFFSET = 12;

void output_picker_draw(PickerSurface *picker, RenderDisplayList *dl) {
    OutputPicker *output = &picker->output;
    OverlaySurface *surface = picker->surface;
    BBox full_surface_box =
        (BBox){0, 0, surface->device_width, surface->device_height};

    if (output->state != OUTPUT_PICKER_ACTIVE) {
        // Pretty close to the default region picker background color.
        // TODO: Rework how style configuration is done (or drop it entirely?)
        RENDER_RECT(
            *dl, .bounds = full_surface_box, .color = {0.025, 0.025, 0.025, 0.4}
        );
    }

    double padding_x = LABEL_PADDING_X * surface->scale / 120.0;
    double padding_y = LABEL_PADDING_Y * surface->scale / 120.0;
    double y_offset = LABEL_Y_OFFSET * surface->scale / 120.0;
    RenderTextMetrics label_size = output->label_size;
    double text_x = (surface->device_width - label_size.width) / 2.0;
    double text_y = output->move_label_down
                        ? (surface->device_height - y_offset - padding_y -
                           label_size.height)
                        : (y_offset + padding_y);

    RENDER_RECT(
        *dl,
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
        *dl,
        .x = text_x,
        .y = text_y,
        .content = output->output_name,
        .length = -1,
        .style = scaled_style,
        .color = RENDER_COLOR_DEFAULT
    );
}

void output_picker_handle_mouse(PickerSurface *picker, MouseEvent event) {
    OutputPicker *output = &picker->output;
    bool should_redraw = false;
    OutputPickerState new_state = event.focus == picker->surface->wl_surface
                                      ? OUTPUT_PICKER_ACTIVE
                                      : OUTPUT_PICKER_INACTIVE;
    if (new_state != output->state) {
        output->state = new_state;
        should_redraw = true;
    }

    // intentionally a bit bigger than the label
    int32_t center_x = picker->surface->logical_width / 2;
    double logical_label_w =
        output->label_size.width / picker->surface->scale * 120.0;
    double logical_label_h =
        output->label_size.height / picker->surface->scale * 120.0;
    bool new_move =
        fabs(center_x - event.surface_x) < logical_label_w / 2.0 + 24 &&
        (event.surface_y) < logical_label_h + LABEL_Y_OFFSET + 24;

    if (output->move_label_down != new_move) {
        output->move_label_down = new_move;
        should_redraw = true;
    }

    if (should_redraw) {
        overlay_surface_queue_draw(picker->surface);
    }

    if (output->state == OUTPUT_PICKER_ACTIVE &&
        event.buttons_released & POINTER_BUTTON_LEFT) {
        picker_context_finish(
            picker, PICKER_FINISH_REASON_SELECTED, (BBox){0, 0, -1, -1}
        );
    }
}

void output_picker_recalculate_label_size(
    PickerSurface *picker, uint32_t scale
) {
    RenderTextStyle scaled_style = RENDER_TEXT_STYLE_DEFAULT(scale);
    picker->output.label_size = picker->surface->renderer->measure_text(
        picker->output.output_name, -1, scaled_style
    );
}

void output_picker_init(PickerSurface *picker) {
    OutputPicker *output = &picker->output;
    if (!output->output_name) {
        // TODO: this should probably be better
        // future pick mode may run this without the background image,
        // and if the output somehow disappeared that pointer would be dangling
        output->output_name = strdup(picker->background->output->name);
        output_picker_recalculate_label_size(picker, picker->surface->scale);
    }
    if (wayland_globals.seat_dispatcher->pointer_data.focus ==
        picker->surface->wl_surface) {
        output->state = OUTPUT_PICKER_ACTIVE;
    } else {
        output->state = OUTPUT_PICKER_INACTIVE;
    }

    seat_dispatcher_set_cursor_for_surface(
        wayland_globals.seat_dispatcher,
        picker->surface,
        WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR
    );
}

void output_picker_destroy(PickerSurface *picker) {
    free(picker->output.output_name);
}
