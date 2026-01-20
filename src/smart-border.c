#include "smart-border.h"
#include "cairo.h"
#include "log.h"
#include <stdatomic.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define IMAGE_ROW(img, y) (img->data + (y) * img->stride)

static int smart_border_context_thread_func(void *data) {
    SmartBorderContext *ctx = data;

    TIMING_START(smart_border);

    int width = ctx->base->width;
    int height = ctx->base->height;
    Image *work_buf_1 = image_convert_format(ctx->base, IMAGE_FORMAT_GRAY8);

    // box blur
    Image *work_buf_2 = image_new(width, height, IMAGE_FORMAT_GRAY8);
    // this is how many pixels to do from the center
    const int BOX_BLUR_SIZE = 8 * ctx->scale / 120;
    log_debug("smart border box blur size %d\n", BOX_BLUR_SIZE);
    for (int y = 0; y < height; y++) {
        const uint8_t *src_row = IMAGE_ROW(work_buf_1, y);
        uint8_t *dest_row = IMAGE_ROW(work_buf_2, y);
        // for a size 2 blur, initial filter state:
        // (p = pixel)
        // vvOvv
        //    pppppp...
        // extend first pixel out
        uint32_t sum = (BOX_BLUR_SIZE + 2) * src_row[0];
        for (int x = 1; x < BOX_BLUR_SIZE; x++) {
            sum += src_row[x];
        }

        for (int x = 0; x < width; x++) {
            sum += src_row[MIN(x + BOX_BLUR_SIZE, width - 1)];
            sum -= src_row[MAX(x - BOX_BLUR_SIZE, 0)];
            dest_row[x] = sum / (2 * BOX_BLUR_SIZE + 1);
        }
    }

    // reuse buffer: Y pass saves in work_buf_1
    for (int x = 0; x < width; x++) {
        // for a size 2 blur, initial filter state:
        // (p = pixel)
        // >
        // >
        // O
        // > p
        // > p
        //   ...
        // extend first pixel out
        uint32_t sum = (BOX_BLUR_SIZE + 2) * IMAGE_ROW(work_buf_2, 0)[x];
        for (int y = 1; y < BOX_BLUR_SIZE; y++) {
            sum += IMAGE_ROW(work_buf_2, y)[x];
        }

        for (int y = 0; y < height; y++) {
            sum += IMAGE_ROW(work_buf_2, MIN(y + BOX_BLUR_SIZE, height - 1))[x];
            sum -= IMAGE_ROW(work_buf_2, MAX(y - BOX_BLUR_SIZE, 0))[x];
            IMAGE_ROW(work_buf_1, y)[x] = sum / (2 * BOX_BLUR_SIZE + 1);
        }
    }

    // quantize
    // this can be done in-place
    for (int y = 0; y < height; y++) {
        uint8_t *row = work_buf_1->data + y * work_buf_1->stride;
        for (int x = 0; x < width; x++) {
            // From some experimentation it seems 0x6f is the best threshold
            row[x] = row[x] < 0x6f ? 0xff : 0x00;
        }
    }

    image_destroy(work_buf_2);

    ctx->result_image = image_convert_format(work_buf_1, ctx->base->format);
    image_destroy(work_buf_1);
    ctx->surface = image_make_cairo_surface(ctx->result_image);
    ctx->pattern = cairo_pattern_create_for_surface(ctx->surface);

    TIMING_END(smart_border);
    atomic_store_explicit(&ctx->is_done, true, memory_order_release);

    return 0;
}

SmartBorderContext *
smart_border_context_start(const Image *base, uint32_t scale) {
    SmartBorderContext *ctx = calloc(1, sizeof(SmartBorderContext));
    ctx->base = base;
    ctx->scale = scale;
    ctx->ref_count = 2;
    thrd_t thread;
    if (thrd_create(&thread, smart_border_context_thread_func, ctx) ==
        thrd_success) {
        thrd_detach(thread);
    }
    return ctx;
}

void smart_border_context_unref(SmartBorderContext *ctx) {
    if (atomic_fetch_sub(&ctx->ref_count, 1) == 1) {
        cairo_pattern_destroy(ctx->pattern);
        cairo_surface_destroy(ctx->surface);
        image_destroy(ctx->result_image);
    }
}
