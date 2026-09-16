#pragma once
#include "picker/common.h"
#include "wayland/overlay-surface.h"
#include "wayland/screen-capture.h"

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

    char *output_name;
    RenderTextMetrics label_size;
    bool move_label_down;

    CaptureFrame *background;
} OutputPicker;

/**
 * Note that the frame is _not_ owned by the @c OutputPicker, and needs to stay
 * alive for as long as the RegionPicker does.
 */
OutputPicker *output_picker_new(
    WrappedOutput *output,
    CaptureFrame *background,
    OutputPickerFinishCallback finish_callback
);
void output_picker_destroy(OutputPicker *picker);
