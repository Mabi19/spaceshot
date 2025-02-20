#pragma once

#include <stdint.h>
#include <wayland-client.h>

/**
 * An image in the XRGB8888 format (little endian).
 */
typedef struct {
    uint8_t *data;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} Image;

Image *image_new(uint32_t width, uint32_t height);
Image *image_new_from_wayland(
    enum wl_shm_format format,
    // This will be copied.
    const uint8_t *data,
    uint32_t width,
    uint32_t height,
    uint32_t stride
);

Image *image_crop(
    const Image *src, uint32_t x, uint32_t y, uint32_t width, uint32_t height
);

void image_save_png(const Image *image, const char *filename);
void image_destroy(Image *image);
