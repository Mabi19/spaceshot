#pragma once
#include "render/command.h"
#include <pango/pango.h>

// Renderer utilities.

void renderer_update_pango_fontdesc(
    PangoFontDescription *fontdesc, RenderTextStyle style
);
