#pragma once
#include "render/renderer.h"
#include <pango/pango.h>

/**
 * A helper struct which contains some objects shared between widgets.
 * Every surface gets its own, though some objects are shared between contexts.
 */
typedef struct {
    Renderer *renderer;
    // These two objects can be shared between all labels.
    PangoFontMap *pango_fontmap;
    // The context can be shared because all our Cairo surfaces are the same.
    PangoContext *pango_context;
} UIContext;

void ui_context_queue_layout(UIContext *context);
void ui_context_queue_draw(UIContext *context);
