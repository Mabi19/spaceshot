#pragma once
#include "image.h"
#include <stdatomic.h>
#include <threads.h>

typedef struct {
    const Image *base;
    uint32_t scale;
    Image *result_image;
    cairo_surface_t *surface;
    cairo_pattern_t *pattern;
    atomic_bool is_done;
    atomic_int ref_count;
} SmartBorderContext;

SmartBorderContext *
smart_border_context_start(const Image *base, uint32_t scale);
void smart_border_context_unref(SmartBorderContext *ctx);
