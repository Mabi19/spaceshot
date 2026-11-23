#include "bbox.h"
#include "render/command.h"
#include "ui/context.h"
#include <stddef.h>

typedef struct WidgetHeader WidgetHeader;

typedef struct {
    /**
     * Update the widget's width and height based on its state,
     * and the positions and layouts of its children.
     * Root widgets have their size forced to the surface size,
     * so they should only act on their children.
     * Required.
     */
    void (*layout)(UIContext *ctx, WidgetHeader *header);
    /**
     * Draw the widget.
     * This function is responsible for applying the coordinate translation
     * itself. Required.
     */
    void (*draw)(
        UIContext *ctx,
        WidgetHeader *header,
        double sx,
        double sy,
        RenderDisplayList *dl
    );

    size_t static_child_count;
    const size_t *static_child_offsets;
} WidgetVTable;

#define STATIC_CHILD_TABLE(table)                                              \
    .static_child_count = sizeof(table) / sizeof(size_t),                      \
    .static_child_offsets = (table)

typedef struct WidgetHeader {
    const WidgetVTable *vtable;
    /**
     * The widget's bounds.
     * The X and Y position are decided by the parent,
     * the width and height are updated by the widget itself in its layout().
     */
    BBox bounds;
} WidgetHeader;

void widget_layout(UIContext *ctx, WidgetHeader *header);
void widget_draw(
    UIContext *ctx,
    WidgetHeader *header,
    double sx,
    double sy,
    RenderDisplayList *dl
);

WidgetHeader *widget_get_child(WidgetHeader *parent, size_t i);
void widget_draw_children(
    UIContext *ctx,
    WidgetHeader *parent,
    double sx,
    double sy,
    RenderDisplayList *dl
);
