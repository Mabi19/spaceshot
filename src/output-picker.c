#include "output-picker.h"
#include "wayland/overlay-surface.h"

OutputPicker *output_picker_new(WrappedOutput *output, Image *background) {}

void output_picker_destroy(OutputPicker *picker) {
    overlay_surface_destroy(picker->surface);
    free(picker);
}
