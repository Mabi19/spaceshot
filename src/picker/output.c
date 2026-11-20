#include "output.h"
#include "alloc.h"
#include "bbox.h"
#include "picker/context.h"
#include "render/command.h"
#include "ui/label.h"
#include "wayland/globals.h"
#include "wayland/overlay-surface.h"
#include <cursor-shape-client.h>
#include <stdlib.h>
#include <xkbcommon/xkbcommon-keysyms.h>

constexpr double LABEL_Y_OFFSET = 12;

static void output_picker_layout(UIContext *ctx, WidgetHeader *header) {
    OutputPicker *output = (OutputPicker *)header;

    widget_layout(ctx, &output->output_tooltip.header);

    double center_x = header->bounds.width / 2;
    BBox *tooltip_bounds = &output->output_tooltip.header.bounds;
    double new_x = center_x - tooltip_bounds->width / 2;
    // TODO: unless label is moved down
    double new_y = LABEL_Y_OFFSET;
    if (tooltip_bounds->x != new_x || tooltip_bounds->y != new_y) {
        tooltip_bounds->x = new_x;
        tooltip_bounds->y = new_y;
        ui_context_queue_draw(ctx);
    }
}

static void output_picker_widget_draw(
    UIContext *ctx,
    WidgetHeader *header,
    double sx,
    double sy,
    RenderDisplayList *dl
) {
    OutputPicker *output = (OutputPicker *)header;

    widget_draw(ctx, &output->output_tooltip.header, sx, sy, dl);
}

const WidgetVTable WIDGET_OUTPUT_PICKER_VTABLE = {
    .layout = output_picker_layout,
    .draw = output_picker_widget_draw,
};

static void output_picker_draw(PickerSurface *picker, RenderDisplayList *dl) {
    OutputPicker *output = &picker->output;
    OverlaySurface *surface = picker->surface;
    BBox full_surface_box =
        (BBox){0, 0, surface->device_width, surface->device_height};

    // TODO: This can also very easily be done by the widget draw function.
    if (output->state != OUTPUT_PICKER_ACTIVE) {
        // Pretty close to the default region picker background color.
        // TODO: Rework how style configuration is done (or drop it entirely?)
        RENDER_RECT(
            *dl, .bounds = full_surface_box, .color = {0.025, 0.025, 0.025, 0.4}
        );
    }
}

static void
output_picker_handle_mouse(PickerSurface *picker, MouseEvent event) {
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
    // int32_t center_x = picker->surface->logical_width / 2;
    // double logical_label_w =
    //     output->label_size.width / picker->surface->scale * 120.0;
    // double logical_label_h =
    //     output->label_size.height / picker->surface->scale * 120.0;
    // bool new_move =
    //     fabs(center_x - event.surface_x) < logical_label_w / 2.0 + 24 &&
    //     (event.surface_y) < logical_label_h + LABEL_Y_OFFSET + 24;

    // if (output->move_label_down != new_move) {
    //     output->move_label_down = new_move;
    //     should_redraw = true;
    // }

    if (should_redraw) {
        overlay_surface_queue_draw(picker->surface);
    }

    if (output->state == OUTPUT_PICKER_ACTIVE &&
        event.buttons_released & POINTER_BUTTON_LEFT) {
        picker_context_finish(
            picker,
            PICKER_FINISH_REASON_SELECTED,
            capture_frame_steal_image(picker->background)
        );
    }
}

static void output_picker_handle_scale(PickerSurface *picker, uint32_t scale) {
    // UITextStyle scaled_style = RENDER_TEXT_STYLE_DEFAULT(scale);
    // picker->output.label_size = picker->surface->renderer->measure_text(
    //     picker->output.output_name, -1, scaled_style
    // );
}

static void output_picker_init(PickerSurface *picker) {
    picker->output.header.vtable = &WIDGET_OUTPUT_PICKER_VTABLE;

    WrappedOutput *output = picker->background->output;
    picker->output.output_name = cstrdup(output->name);
    widget_tooltip_init(&picker->ui, &picker->output.output_tooltip);
    widget_label_set_font(
        &picker->ui, &picker->output.output_tooltip.label, "Sans", 16.0
    );
    widget_label_set_content(
        &picker->ui,
        &picker->output.output_tooltip.label,
        picker->output.output_name,
        -1
    );

    output_picker_handle_scale(picker, picker->surface->scale);
}

static void output_picker_enter(PickerSurface *picker) {
    OutputPicker *output = &picker->output;
    picker->ui.picker_root = &output->header;

    if (wayland_globals.seat->pointer_data.focus ==
        picker->surface->wl_surface) {
        output->state = OUTPUT_PICKER_ACTIVE;
    } else {
        output->state = OUTPUT_PICKER_INACTIVE;
    }

    overlay_surface_set_cursor(
        picker->surface, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR
    );
}

void output_picker_destroy(PickerSurface *picker) {
    if (picker->output.output_name) {
        free(picker->output.output_name);
    }
}

const PickerVTable output_picker_vtable = {
    .per_output = true,
    .init = output_picker_init,
    .enter = output_picker_enter,
    .draw = output_picker_draw,
    .mouse = output_picker_handle_mouse,
    .scale = output_picker_handle_scale,
};
