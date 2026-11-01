#pragma once
#include "render/renderer.h"

// TODO: The PangoFontMap and PangoContext should live in here.
// They're shared between all labels.
typedef struct {
    Renderer *renderer;
    double scale;
} UIContext;
