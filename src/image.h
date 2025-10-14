#pragma once

#include "link-buffer.h"
#include <cairo.h>
#include <stdint.h>
#include <wayland-client.h>

constexpr uint32_t IMAGE_FORMAT_FLIPPED_ORDER = (1 << 15);
/**
 * An enum of image formats.
 * BGR formats are the corresponding RGB formats OR'd with
 * IMAGE_FORMAT_FLIPPED_ORDER. Note that all formats here are little endian.
 */
typedef enum {
    IMAGE_FORMAT_XRGB8888 = 1,
    IMAGE_FORMAT_ARGB8888 = 2,
    IMAGE_FORMAT_XRGB2101010 = 3,
    IMAGE_FORMAT_XBGR2101010 =
        IMAGE_FORMAT_XRGB2101010 | IMAGE_FORMAT_FLIPPED_ORDER,
    IMAGE_FORMAT_GRAY8 = 4,
} ImageFormat;

ImageFormat image_format_from_wl(enum wl_shm_format format);
enum wl_shm_format image_format_to_wl(ImageFormat format);
/**
 * Note that this function always returns an RGB format.
 * Manually specified colors should have R & B flipped if
 * (format & IMAGE_FORMAT_FLIPPED_ORDER).
 */
cairo_format_t image_format_to_cairo(ImageFormat format);
uint32_t image_format_bytes_per_pixel(ImageFormat format);

typedef struct {
    uint8_t *data;
    ImageFormat format;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} Image;

Image *image_new(uint32_t width, uint32_t height, ImageFormat format);
Image *image_new_from_wayland(
    enum wl_shm_format wl_format,
    // This will be copied.
    const uint8_t *data,
    uint32_t width,
    uint32_t height,
    uint32_t stride
);
void image_destroy(Image *image);

Image *image_copy(const Image *src);

Image *image_crop(
    const Image *src, uint32_t x, uint32_t y, uint32_t width, uint32_t height
);

Image *image_convert_format(const Image *src, ImageFormat target);

/**
 * Create a Cairo surface for an image. Note that the data isn't copied, so the
 * image needs to stay alive for as long as the cairo_surface_t.
 */
cairo_surface_t *image_make_cairo_surface(Image *image);

LinkBuffer *image_save_png(const Image *image);
