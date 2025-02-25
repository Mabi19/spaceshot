#include "image.h"
#include <assert.h>
#include <cairo.h>
#include <spng.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const uint32_t BYTES_PER_PIXEL = 4;

Image *image_new(uint32_t width, uint32_t height) {
    Image *result = calloc(1, sizeof(Image));
    if (!result) {
        return NULL;
    }

    result->width = width;
    result->height = height;
    result->stride = cairo_format_stride_for_width(CAIRO_FORMAT_RGB24, width);
    result->data = malloc(result->stride * height);
    return result;
}

Image *image_new_from_wayland(
    enum wl_shm_format format,
    // This will be copied.
    const uint8_t *data,
    uint32_t width,
    uint32_t height,
    uint32_t stride
) {
    if (format == WL_SHM_FORMAT_XRGB8888) {
        Image *result = image_new(width, height);
        if (!result)
            return NULL;

        // copy the data
        const uint8_t *source_row = data;
        uint8_t *result_row = result->data;
        for (uint32_t y = 0; y < height; y++) {
            memcpy(result_row, source_row, width * BYTES_PER_PIXEL);
            source_row += stride;
            result_row += result->stride;
        }

        return result;
    } else {
        fprintf(
            stderr,
            "Error: Couldn't create image object out of wl_shm format %d\n",
            format
        );
        exit(EXIT_FAILURE);
    }
}

Image *image_crop(
    const Image *src, uint32_t x, uint32_t y, uint32_t width, uint32_t height
) {
    uint32_t crop_right = x + width;
    uint32_t crop_bottom = y + height;
    assert(crop_right <= src->width && crop_bottom <= src->height);

    Image *result = image_new(width, height);
    if (!result) {
        return NULL;
    }

    for (uint32_t crop_y = 0; crop_y < height; crop_y++) {
        const uint8_t *source_row =
            src->data + (y + crop_y) * src->stride + x * BYTES_PER_PIXEL;
        uint8_t *result_row = result->data + crop_y * result->stride;
        memcpy(result_row, source_row, width * BYTES_PER_PIXEL);
    }
    return result;
}

void image_save_png(const Image *image, const char *filename) {
    spng_ctx *ctx = spng_ctx_new(SPNG_CTX_ENCODER);
    struct spng_ihdr ihdr = {0};
    ihdr.width = image->width;
    ihdr.height = image->height;
    ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR;
    ihdr.bit_depth = 8;
    spng_set_ihdr(ctx, &ihdr);

    FILE *out_file = fopen(filename, "w");
    spng_set_png_file(ctx, out_file);

    // libspng expects 3 bytes per pixel
    uint8_t *data = malloc(image->width * image->height * 3);
    uint8_t *current_pixel = data;
    for (uint32_t y = 0; y < image->height; y++) {
        for (uint32_t x = 0; x < image->width; x++) {
            uint8_t *source_pixel =
                image->data + y * image->stride + x * BYTES_PER_PIXEL;
            current_pixel[0] = source_pixel[2];
            current_pixel[1] = source_pixel[1];
            current_pixel[2] = source_pixel[0];
            current_pixel += 3;
        }
    }

    int encode_result = spng_encode_image(
        ctx,
        data,
        image->width * image->height * 3,
        SPNG_FMT_RAW,
        SPNG_ENCODE_FINALIZE
    );
    free(data);
    fclose(out_file);
    spng_ctx_free(ctx);

    if (encode_result) {
        fprintf(stderr, "spng error: %s\n", spng_strerror(encode_result));
        exit(EXIT_FAILURE);
    }
}

void image_destroy(Image *image) {
    free(image->data);
    free(image);
}
