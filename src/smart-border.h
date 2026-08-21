#pragma once
#include "image.h"
#include "render/texture.h"
#include <stdatomic.h>
#include <threads.h>

typedef struct {
    const Image *base;
    uint32_t scale;
    Image *result_image;
    RenderTexture *result_texture;
    atomic_bool is_done;
    atomic_int ref_count;
} SmartBorderContext;

SmartBorderContext *
smart_border_context_start(const Image *base, uint32_t scale);
void smart_border_context_unref(SmartBorderContext *ctx);
