#include "label.h"
#include "render/command.h"
#include "ui/context.h"
#include <pango/pango.h>

static void
widget_label_layout(UIContext *ctx, WidgetHeader *header, double scale) {
    WidgetLabel *label = (WidgetLabel *)header;
    // TODO: recompute Pango parameters, if anything's different queue a draw
}

static void widget_label_draw(
    UIContext *ctx,
    WidgetHeader *header,
    double sx,
    double sy,
    double scale,
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
