#pragma once
#include "render/renderer.h"
#include "wayland/overlay-surface.h"
#include <pango/pango.h>

/** Common data to initialize UI contexts out of. */
typedef struct {
    const Renderer *renderer;
    // These two objects can be shared between all labels.
    PangoFontMap *pango_fontmap;
    // The context can be shared because all our Cairo surfaces are the same.
    PangoContext *pango_context;
} UIContextCommon;

typedef struct WidgetHeader WidgetHeader;

/**
 * A helper struct which contains some objects shared between widgets.
 * Every surface gets its own, though some objects are shared between contexts.
 */
typedef struct {
    const Renderer *renderer;
    // These two objects can be shared between all labels.
    PangoFontMap *pango_fontmap;
    // The context can be shared because all our Cairo surfaces are the same.
    PangoContext *pango_context;

    /**
     * The picker's root widget. May be NULL
     * if the picker doesn't use widgets.
     */
    WidgetHeader *picker_root;

    /**
     * The UI scale. All drawing coordinates should be multiplied by this number
     * to convert from logical coordinates to device coordinates.
     */
    double scale;

    bool layout_queued;
    bool draw_queued;
} UIContext;

void ui_context_init_common(UIContextCommon *common);
void ui_context_init(UIContext *ctx, UIContextCommon *common);
// TODO: ui_context_deinit (_common)

void ui_context_queue_layout(UIContext *context);
void ui_context_queue_draw(UIContext *context);
void ui_context_dispatch_queued(UIContext *context, OverlaySurface *surface);
