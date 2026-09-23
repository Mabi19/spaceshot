#pragma once
#include "image.h"
#include "render/renderer.h"
#include "render/texture.h"
#include "wayland/screen-capture.h"
#include <stdatomic.h>
#include <threads.h>

typedef struct {
    const Renderer *renderer;
    const Image *base;
    uint32_t scale;
    Image *result_image;
    RenderTexture *result_texture;
    // Synchronization primitives to support both:
    // (a) atomically checking whether the generation is done,
    // (b) waiting until it's safe to destroy the input frame.
    mtx_t base_mtx;
    cnd_t base_cnd;
    bool base_released;
    atomic_bool is_done;
    atomic_int ref_count;
} SmartBorderContext;

/**
 * Start computing the smart border image.
 * When it's done, the is_done property of the context will be set to true,
 * and result_image will contain the result.
 * result_texture will be NULL, because textures can't be created off-thread.
 */
SmartBorderContext *
smart_border_context_start(CaptureFrame *base, uint32_t scale);
/** Wait until the input image is done being used and can be safely deleted. */
void smart_border_context_ensure_safe_delete(SmartBorderContext *ctx);
void smart_border_context_unref(SmartBorderContext *ctx);
