#include "image.h"
#include "log.h"
#include <assert.h>
#include <cairo.h>
#include <png.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>

// image format conversions

ImageFormat image_format_from_wl(enum wl_shm_format format) {
    switch (format) {
    case WL_SHM_FORMAT_XRGB8888:
        return IMAGE_FORMAT_XRGB8888;
    case WL_SHM_FORMAT_XRGB2101010:
        return IMAGE_FORMAT_XRGB2101010;
    case WL_SHM_FORMAT_XBGR2101010:
        return IMAGE_FORMAT_XBGR2101010;
    default:
        REPORT_UNHANDLED("wl format", "%x", format);
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
        REPORT_UNHANDLED("image format", "%x", format);
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
        REPORT_UNHANDLED("image format", "%x", format);
    }
}
uint32_t image_format_bytes_per_pixel(ImageFormat format) {
    switch (format) {
    case IMAGE_FORMAT_XRGB8888:
    case IMAGE_FORMAT_XRGB2101010:
    case IMAGE_FORMAT_XBGR2101010:
        return 4;
    default:
        REPORT_UNHANDLED("image format", "%x", format);
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

static int timespec_subtract(
    struct timespec *result, struct timespec *x, struct timespec *y
) {
    /* Perform the carry for the later subtraction by updating y. */
    if (x->tv_nsec < y->tv_nsec) {
        int nsec = (y->tv_nsec - x->tv_nsec) / 1000000000 + 1;
        y->tv_nsec -= 1000000000 * nsec;
        y->tv_sec += nsec;
    }
    if (x->tv_nsec - y->tv_nsec > 1000000000) {
        int nsec = (x->tv_nsec - y->tv_nsec) / 1000000000;
        y->tv_nsec += 1000000000 * nsec;
        y->tv_sec -= nsec;
    }

    /* Compute the time remaining to wait.
       tv_nsec is certainly positive. */
    result->tv_sec = x->tv_sec - y->tv_sec;
    result->tv_nsec = x->tv_nsec - y->tv_nsec;

    /* Return 1 if result is negative. */
    return x->tv_sec < y->tv_sec;
}

void image_save_png(
    const Image *image, void **output_buf, size_t *output_size
) {
    png_structp png_data =
        png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_data) {
        report_error_fatal("libpng error: couldn't create png_structp");
    }

    png_infop png_info = png_create_info_struct(png_data);
    if (!png_info) {
        report_error_fatal("libpng: couldn't create png_infop");
    }

    if (setjmp(png_jmpbuf(png_data))) {
        report_error_fatal("libpng error");
    }

    int png_bit_depth;
    switch (image->format) {
    case IMAGE_FORMAT_XRGB8888:
        png_bit_depth = 8;
        break;
    case IMAGE_FORMAT_XRGB2101010:
    case IMAGE_FORMAT_XBGR2101010:
        png_bit_depth = 16;
        break;
    default:
        REPORT_UNHANDLED("image format", "%x", image->format);
    }

    struct timespec t_start;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    void *transcoded_data;
    uint32_t transcoded_len;
    uint32_t bytes_per_pixel = image_format_bytes_per_pixel(image->format);
    if (image->format == IMAGE_FORMAT_XRGB8888) {
        // libspng expects 3 bytes per pixel
        transcoded_len = image->width * image->height * 3;
        uint8_t *data = malloc(transcoded_len);
        uint8_t *current_pixel = data;
        for (uint32_t y = 0; y < image->height; y++) {
            for (uint32_t x = 0; x < image->width; x++) {
                uint32_t *source_pixel =
                    (uint32_t *)(image->data + y * image->stride +
                                 x * bytes_per_pixel);
                current_pixel[0] = (*source_pixel >> 16) & 0xff;
                current_pixel[1] = (*source_pixel >> 8) & 0xff;
                current_pixel[2] = *source_pixel & 0xff;
                current_pixel += 3;
            }
        }
        transcoded_data = data;
    } else if (image->format == IMAGE_FORMAT_XRGB2101010) {
        // libspng expects 6 bytes per pixel
        transcoded_len = image->width * image->height * 6;
        uint16_t *data = malloc(transcoded_len);
        uint16_t *current_pixel = data;
        for (uint32_t y = 0; y < image->height; y++) {
            for (uint32_t x = 0; x < image->width; x++) {
                uint32_t *source_pixel =
                    (uint32_t *)(image->data + y * image->stride +
                                 x * bytes_per_pixel);
                current_pixel[0] = ((*source_pixel >> 20) & 0x3ff) << 6;
                current_pixel[1] = ((*source_pixel >> 10) & 0x3ff) << 6;
                current_pixel[2] = (*source_pixel & 0x3ff) << 6;
                current_pixel += 3;
            }
        }
        transcoded_data = data;
    } else if (image->format == IMAGE_FORMAT_XBGR2101010) {
        // libspng expects 6 bytes per pixel
        transcoded_len = image->width * image->height * 6;
        uint16_t *data = malloc(transcoded_len);
        uint16_t *current_pixel = data;
        for (uint32_t y = 0; y < image->height; y++) {
            for (uint32_t x = 0; x < image->width; x++) {
                uint32_t *source_pixel =
                    (uint32_t *)(image->data + y * image->stride +
                                 x * bytes_per_pixel);
                current_pixel[0] = (*source_pixel & 0x3ff) << 6;
                current_pixel[1] = ((*source_pixel >> 10) & 0x3ff) << 6;
                current_pixel[2] = ((*source_pixel >> 20) & 0x3ff) << 6;
                current_pixel += 3;
            }
        }
        transcoded_data = data;
    } else {
        REPORT_UNHANDLED("image format", "%x", image->format);
    }

    struct timespec t_transcode;
    clock_gettime(CLOCK_MONOTONIC, &t_transcode);
    struct timespec t_diff;
    timespec_subtract(&t_diff, &t_transcode, &t_start);
    fprintf(
        stderr, "transcode took %lds %09ldns\n", t_diff.tv_sec, t_diff.tv_nsec
    );

    int encode_result = spng_encode_image(
        ctx, transcoded_data, transcoded_len, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE
    );
    free(transcoded_data);

    struct timespec t_image;
    clock_gettime(CLOCK_MONOTONIC, &t_image);
    struct timespec t_diff2;
    timespec_subtract(&t_diff2, &t_image, &t_transcode);
    fprintf(
        stderr,
        "png encode took %lds %09ldns\n",
        t_diff2.tv_sec,
        t_diff2.tv_nsec
    );

    if (encode_result) {
        report_error_fatal("spng error: %s", spng_strerror(encode_result));
    }

    int buffer_result;
    *output_buf = spng_get_png_buffer(ctx, output_size, &buffer_result);
    if (buffer_result) {
        report_error_fatal("spng error: %s", spng_strerror(buffer_result));
    }
    spng_ctx_free(ctx);
}

void image_destroy(Image *image) {
    free(image->data);
    free(image);
}
