#pragma once

#include <stdint.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

/**
 * An image in the rgb(a?) format.
 * If has_alpha, then a is alpha and there are 4 bytes per pixel, otherwise
 * there are 3 bytes per pixel.
 */
typedef struct {
    uint8_t *data;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    bool has_alpha;
} Image;

Image *image_create(uint32_t width, uint32_t height, bool has_alpha);
Image *image_create_from_wayland(
    enum wl_shm_format format,
    // This will be copied.
    uint8_t *data,
    uint32_t width,
    uint32_t height,
    uint32_t stride
);

void image_save_png(const Image *image, const char *filename);
void image_free(Image *image);
