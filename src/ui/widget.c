#include "widget.h"
#include "ui/context.h"

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

WidgetHeader *widget_get_child(WidgetHeader *parent, size_t i) {
    size_t static_count = parent->vtable->static_child_count;
    if (i < static_count) {
        return (WidgetHeader *)((uintptr_t)parent +
                                parent->vtable->static_child_offsets[i]);
    }
    i -= static_count;
    // TODO: dynamic children
    return NULL;
}
void widget_draw_children(
    UIContext *ctx,
    WidgetHeader *parent,
    double sx,
    double sy,
    RenderDisplayList *dl
) {
    size_t i = 0;
    WidgetHeader *child;
    while ((child = widget_get_child(parent, i++))) {
        widget_draw(
            ctx, child, sx + parent->bounds.x, sy + parent->bounds.y, dl
        );
    }
}
