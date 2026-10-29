#include "bbox.h"
#include "render/command.h"
#include <stddef.h>

// TODO: Think some more about this.

typedef struct {
    double scale;
} UIContext;

typedef struct WidgetHeader WidgetHeader;

typedef struct {
    /**
     * Update the widget's layout based on the bounds given it.
     * Required.
     */
    void (*layout)(UIContext *context, BBox bounds);
    /**
     * Draw the widget.
     * This function is responsible for applying the coordinate translation
     * itself. Optional: by default draws children.
     */
    void (*draw)(
        UIContext *context, double sx, double sy, RenderDisplayList *dl
    );
    /**
     * Decide whether the specified event should go to this widget.
     * TODO: this only really makes sense for clicks/touch.
     * Keyboard should probably be handled externally, keys aren't
     * pointed at a specific widget (we do not have keyboard focus).
     */
    bool (*event_capture)();
    void (*click)();
    void (*mousemove)(double x, double y);
    void (*mouseout)();
} WidgetVTable;

typedef struct {
    /** The amount of items in the offsets array. */
    size_t count;
    /** Offsets into the widget at which child widgets live. */
    size_t offsets[];
} WidgetChildTable;

typedef struct WidgetHeader {
    const WidgetVTable *vtable;
    const WidgetChildTable *children;
    // TODO: bounds.
    // How do I want to do this?
    // I don't do any size-constrained layout,
    // widgets are always constant size.
    // But it would be very nice to be able to cull things which are outside the
    // clipping region. (Actually this would be the responsibility of the
    // renderer) Event delivery would love to have bounds as well so it knows
    // where to target (but event capture SHOULD be customizable, for rounding
    // reasons!)
    BBox bounds;
} WidgetHeader;
