#include "image.h"
#include <assert.h>
#include <cairo.h>
#include <spng.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client-protocol.h>

// image format conversions
// TODO: introduce a nicer "assert unreachable" function/macro

ImageFormat image_format_from_wl(enum wl_shm_format format) {
    switch (format) {
    case WL_SHM_FORMAT_XRGB8888:
        return IMAGE_FORMAT_XRGB8888;
    case WL_SHM_FORMAT_XRGB2101010:
        return IMAGE_FORMAT_XRGB2101010;
    case WL_SHM_FORMAT_XBGR2101010:
        return IMAGE_FORMAT_XBGR2101010;
    default:
        fprintf(stderr, "internal error: unhandled wl format %x\n", format);
        exit(EXIT_FAILURE);
    }
}
enum wl_shm_format image_format_to_wl(ImageFormat format) {
    switch (format) {
    case IMAGE_FORMAT_XRGB8888:
        return WL_SHM_FORMAT_XRGB8888;
    case IMAGE_FORMAT_XRGB2101010:
        return WL_SHM_FORMAT_XRGB2101010;
    case IMAGE_FORMAT_XBGR2101010:
        return WL_SHM_FORMAT_XBGR2101010;
    default:
        fprintf(stderr, "internal error: unhandled image format %x\n", format);
        exit(EXIT_FAILURE);
    }
}
cairo_format_t image_format_to_cairo(ImageFormat format) {
    switch (format) {
    case IMAGE_FORMAT_XRGB8888:
        return CAIRO_FORMAT_RGB24;
    case IMAGE_FORMAT_XRGB2101010:
    case IMAGE_FORMAT_XBGR2101010:
        return CAIRO_FORMAT_RGB30;
    default:
        fprintf(stderr, "internal error: unhandled image format %x\n", format);
        exit(EXIT_FAILURE);
    }
}
uint32_t image_format_bytes_per_pixel(ImageFormat format) {
    switch (format) {
    case IMAGE_FORMAT_XRGB8888:
    case IMAGE_FORMAT_XRGB2101010:
    case IMAGE_FORMAT_XBGR2101010:
        return 4;
    default:
        fprintf(stderr, "internal error: unhandled image format %x\n", format);
        exit(EXIT_FAILURE);
    }
}

Image *image_new(uint32_t width, uint32_t height, ImageFormat format) {
    Image *result = calloc(1, sizeof(Image));
    if (!result) {
        return NULL;
    }

    result->format = format;
    result->width = width;
    result->height = height;
    result->stride =
        cairo_format_stride_for_width(image_format_to_cairo(format), width);
    result->data = malloc(result->stride * height);
    return result;
}

Image *image_new_from_wayland(
    enum wl_shm_format wl_format,
    // This will be copied.
    const uint8_t *data,
    uint32_t width,
    uint32_t height,
    uint32_t stride
) {
    ImageFormat format = image_format_from_wl(wl_format);

    Image *result = image_new(width, height, format);
    if (!result)
        return NULL;

    // copy the data
    uint32_t bytes_per_pixel = image_format_bytes_per_pixel(format);
    const uint8_t *source_row = data;
    uint8_t *result_row = result->data;
    for (uint32_t y = 0; y < height; y++) {
        memcpy(result_row, source_row, width * bytes_per_pixel);
        source_row += stride;
        result_row += result->stride;
    }

    return result;
}

Image *image_crop(
    const Image *src, uint32_t x, uint32_t y, uint32_t width, uint32_t height
) {
    uint32_t crop_right = x + width;
    uint32_t crop_bottom = y + height;
    assert(x < src->width && y < src->height);
    assert(crop_right <= src->width && crop_bottom <= src->height);

    Image *result = image_new(width, height, src->format);
    if (!result) {
        return NULL;
    }

    uint32_t bytes_per_pixel = image_format_bytes_per_pixel(src->format);
    for (uint32_t crop_y = 0; crop_y < height; crop_y++) {
        const uint8_t *source_row =
            src->data + (y + crop_y) * src->stride + x * bytes_per_pixel;
        uint8_t *result_row = result->data + crop_y * result->stride;
        memcpy(result_row, source_row, width * bytes_per_pixel);
    }
    return result;
}

cairo_surface_t *image_make_cairo_surface(Image *image) {
    return cairo_image_surface_create_for_data(
        image->data,
        image_format_to_cairo(image->format),
        image->width,
        image->height,
        image->stride
    );
}

void image_save_png(const Image *image, const char *filename) {
    spng_ctx *ctx = spng_ctx_new(SPNG_CTX_ENCODER);
    struct spng_ihdr ihdr = {0};
    ihdr.width = image->width;
    ihdr.height = image->height;
    ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR;
    switch (image->format) {
    case IMAGE_FORMAT_XRGB8888:
        ihdr.bit_depth = 8;
        break;
    case IMAGE_FORMAT_XRGB2101010:
    case IMAGE_FORMAT_XBGR2101010:
        ihdr.bit_depth = 16;
        break;
    default:
        fprintf(stderr, "internal error: unhandled format %d\n", image->format);
        exit(EXIT_FAILURE);
    }
    spng_set_ihdr(ctx, &ihdr);

    FILE *out_file = fopen(filename, "w");
    spng_set_png_file(ctx, out_file);

    void *result_data;
    uint32_t result_len;
    uint32_t bytes_per_pixel = image_format_bytes_per_pixel(image->format);
    if (image->format == IMAGE_FORMAT_XRGB8888) {
        // libspng expects 3 bytes per pixel
        result_len = image->width * image->height * 3;
        uint8_t *data = malloc(result_len);
        uint8_t *current_pixel = data;
        for (uint32_t y = 0; y < image->height; y++) {
            for (uint32_t x = 0; x < image->width; x++) {
                uint8_t *source_pixel =
                    image->data + y * image->stride + x * bytes_per_pixel;
                current_pixel[0] = source_pixel[2];
                current_pixel[1] = source_pixel[1];
                current_pixel[2] = source_pixel[0];
                current_pixel += 3;
            }
        }
        result_data = data;
    } else if (image->format == IMAGE_FORMAT_XRGB2101010) {
        // libspng expects ??? bytes per pixel
        fprintf(stderr, "TODO: 16-bit save\n");
        exit(EXIT_FAILURE);
    } else if (image->format == IMAGE_FORMAT_XBGR2101010) {
        // libspng expects ??? bytes per pixel
        fprintf(stderr, "TODO: 16-bit save\n");
        exit(EXIT_FAILURE);
    } else {
        fprintf(stderr, "internal error: unhandled format %d\n", image->format);
        exit(EXIT_FAILURE);
    }

    int encode_result = spng_encode_image(
        ctx, result_data, result_len, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE
    );
    free(result_data);
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
