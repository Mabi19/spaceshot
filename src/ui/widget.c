#include "widget.h"

void widget_layout(UIContext *ctx, WidgetHeader *header) {
    header->vtable->layout(ctx, header);
}
void widget_draw(
    UIContext *ctx,
    WidgetHeader *header,
    double sx,
    double sy,
    RenderDisplayList *dl
) {
    header->vtable->draw(ctx, header, sx, sy, dl);
}
