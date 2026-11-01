#include "bbox.h"
#include "render/command.h"
#include "ui/context.h"
#include <stddef.h>

typedef struct WidgetHeader WidgetHeader;

typedef struct {
    /**
     * Update the widget's width and height based on its state,
     * and the positions of its children.
     * Required.
     */
    void (*layout)(UIContext *context, WidgetHeader *header, double scale);
    /**
     * Draw the widget.
     * This function is responsible for applying the coordinate translation
     * itself. Optional: by default draws children.
     */
    void (*draw)(
        UIContext *context,
        WidgetHeader *header,
        double sx,
        double sy,
        double scale,
        RenderDisplayList *dl
    );
} WidgetVTable;

typedef struct WidgetHeader {
    const WidgetVTable *vtable;
    /**
     * The widget's bounds.
     * The X and Y position are decided by the parent,
     * the width and height are updated by the widget itself in its layout().
     */
    BBox bounds;
} WidgetHeader;
