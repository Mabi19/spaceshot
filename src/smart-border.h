#pragma once
#include "image.h"
#include <stdatomic.h>
#include <threads.h>

typedef struct {
    const Image *base;
    Image *result_image;
    cairo_surface_t *surface;
    cairo_pattern_t *pattern;
    atomic_bool is_done;
    atomic_int ref_count;
} SmartBorderContext;

SmartBorderContext *smart_border_context_start(Image *base);
void smart_border_context_unref(SmartBorderContext *ctx);
