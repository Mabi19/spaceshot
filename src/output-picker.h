#pragma once
#include "picker-common.h"
#include "wayland/overlay-surface.h"

struct OutputPicker;

typedef void (*OutputPickerFinishCallback)(
    struct OutputPicker *picker, PickerFinishReason reason
);

typedef enum {
    OUTPUT_PICKER_INACTIVE,
    OUTPUT_PICKER_ACTIVE,
    OUTPUT_PICKER_UNINITIALIZED,
} OutputPickerState;

typedef struct OutputPicker {
    OverlaySurface *surface;

    OutputPickerState state;
    OutputPickerFinishCallback finish_callback;

    LinkBuffer *command_arena;

    const char *output_name;
    RenderTextMetrics label_size;
    bool move_label_down;

    const Image *background_image;
    RenderTexture *background_texture;
} OutputPicker;

/**
 * Note that the image is _not_ owned by the @c OutputPicker, and needs to stay
 * alive for as long as the RegionPicker does.
 */
OutputPicker *output_picker_new(
    WrappedOutput *output,
    Image *background,
    OutputPickerFinishCallback finish_callback
);
void output_picker_destroy(OutputPicker *picker);
