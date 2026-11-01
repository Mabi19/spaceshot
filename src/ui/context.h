#pragma once
#include "render/renderer.h"
#include <pango/pango.h>

typedef struct {
    Renderer *renderer;
    // These two objects can be shared between all labels.
    PangoFontMap *pango_fontmap;
    PangoContext *pango_context;
} UIContext;
