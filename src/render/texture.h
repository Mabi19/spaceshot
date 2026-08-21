#pragma once
#include "image.h"
#include <cairo.h>

typedef struct {
    cairo_surface_t *cr_surface;
    cairo_pattern_t *cr_pattern;
} RenderTexture;

RenderTexture *render_texture_new_from_image(const Image *image);
void render_texture_destroy(const RenderTexture *texture);
