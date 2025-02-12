#include "image.h"
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Image *image_new(uint32_t width, uint32_t height, bool has_alpha) {
    Image *result = calloc(1, sizeof(Image));
    if (!result) {
        return NULL;
    }

    uint32_t bytes_per_pixel = has_alpha ? 4 : 3;
    result->width = width;
    result->height = height;
    result->stride = bytes_per_pixel * width;
    result->has_alpha = has_alpha;
    result->data = malloc(result->stride * height);
    return result;
}

Image *image_new_from_wayland(
    enum wl_shm_format format,
    // This will be copied.
    uint8_t *data,
    uint32_t width,
    uint32_t height,
    uint32_t stride
) {
    if (format == WL_SHM_FORMAT_XRGB8888) {
        Image *result = image_new(width, height, false);
        if (!result)
            return NULL;

        // copy the data
        uint8_t *source_row = data;
        uint8_t *result_row = result->data;
        for (uint32_t y = 0; y < height; y++) {
            for (uint32_t x = 0; x < width; x++) {
                // note: this format is little-endian

                // r
                result_row[x * 3] = source_row[x * 4 + 2];
                // g
                result_row[x * 3 + 1] = source_row[x * 4 + 1];
                // b
                result_row[x * 3 + 2] = source_row[x * 4 + 0];
            }

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

void image_save_png(const Image *image, const char *filename) {
    png_image png_data;
    memset(&png_data, 0, sizeof(png_data));
    png_data.version = PNG_IMAGE_VERSION;
    png_data.width = image->width;
    png_data.height = image->height;
    png_data.format = image->has_alpha ? PNG_FORMAT_RGBA : PNG_FORMAT_RGB;
    png_image_write_to_file(
        &png_data, filename, 0, image->data, image->stride, NULL
    );
    if (png_data.warning_or_error != 0) {
        fprintf(stderr, "PNG error: %s\n", png_data.message);
        exit(EXIT_FAILURE);
    }
}

void image_destroy(Image *image) {
    free(image->data);
    free(image);
}
