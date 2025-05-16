#include "output-picker.h"
#include "wayland/overlay-surface.h"
#include "wayland/shared-memory.h"

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

    return result;
}

void output_picker_destroy(OutputPicker *picker) {
    overlay_surface_destroy(picker->surface);
    free(picker);
}
