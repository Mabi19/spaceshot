#pragma once
#include "render/command.h"
#include <pango/pango.h>

/**
 * Mutate a RECT command in-place so its parameters make sense:
 * borders and border radii are not larger than the rectangle itself.
 */
void renderer_sanitize_rect(RenderCommandRect *rect);
