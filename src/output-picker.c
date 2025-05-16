#include "output-picker.h"
#include "wayland/overlay-surface.h"
#include "wayland/render.h"
#include "wayland/shared-memory.h"
#include <cairo.h>

// TODO: Pretty much everything.

static void output_picker_render(void *user_data) {
    OutputPicker *picker = user_data;
}

static void output_picker_close(void *user_data) {
    // TODO
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
            .draw = NULL,
            .manual_render = output_picker_render,
            .close = output_picker_close,
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

    return result;
}

void output_picker_destroy(OutputPicker *picker) {
    overlay_surface_destroy(picker->surface);
    free(picker);
}
