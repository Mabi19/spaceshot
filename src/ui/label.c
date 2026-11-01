#include "label.h"
#include "render/command.h"
#include "ui/context.h"
#include <pango/pango.h>

typedef struct {
    double width;
    double height;
} UITextMetrics;

typedef struct {

} UITextStyle;

/**
 * Measure a piece of text's width and height.
 * The string is length-based,
 * but you can set length to -1 to deduce it via strlen.
 */
UITextMetrics measure_text(const char *content, int length, UITextStyle style) {
    // TODO (perhaps this won't be necessary...)
}

static void widget_label_layout(UIContext *ctx, WidgetHeader *header) {
    WidgetLabel *label = (WidgetLabel *)header;
    // TODO: recompute Pango parameters, if anything's different queue a draw
}

static void widget_label_draw(
    UIContext *ctx,
    WidgetHeader *header,
    double sx,
    double sy,
    RenderDisplayList *dl
) {
    WidgetLabel *label = (WidgetLabel *)header;

    RENDER_RECT(
        *dl,
        .bounds =
            (BBox){
                .x = round(header->bounds.x + sx),
                .y = round(header->bounds.y + sy),
                .width = label->rendered->width,
                .height = label->rendered->height,
            },
        .color = label->color,
        .texture = label->texture,
        .uv = RENDER_UV_DEFAULT
    );
}

const WidgetVTable WIDGET_LABEL_VTABLE = {
    .layout = widget_label_layout,
    .draw = widget_label_draw,
};
