#pragma once
#include "image.h"
#include "render/renderer.h"
#include "render/texture.h"
#include <stdatomic.h>
#include <threads.h>

typedef struct {
    const Renderer *renderer;
    const Image *base;
    uint32_t scale;
    Image *result_image;
    RenderTexture *result_texture;
    atomic_bool is_done;
    atomic_int ref_count;
} SmartBorderContext;

SmartBorderContext *smart_border_context_start(
    const Renderer *renderer, const Image *base, uint32_t scale
);
void smart_border_context_unref(SmartBorderContext *ctx);
