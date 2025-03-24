#include "image.h"
#include "link-buffer.h"
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

static void
write_png_data(png_structp png_data, png_bytep data, png_size_t length) {
    link_buffer_append(png_get_io_ptr(png_data), data, length);
}

static void flush_png_data(png_structp /* png_data */) {
    // there is no file, so no need to flush
}

LinkBuffer *image_save_png(const Image *image) {
    // Writing to a link buffer can change the current block, so the start needs
    // to be saved
    LinkBuffer *result = link_buffer_new();
    LinkBuffer *curr_block = result;

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

    // Lowering the compression level (default = 6) results in about 33% faster
    // encoding in my testing, with a not very significant size hit.
    // TODO: make this configurable
    png_set_compression_level(png_data, 4);
    // png_init_io(png_data, wrapped_fd);
    png_set_write_fn(png_data, &curr_block, write_png_data, flush_png_data);

    // set up all the metadata

    int png_bit_depth, png_significant_bits;
    switch (image->format) {
    case IMAGE_FORMAT_XRGB8888:
        png_bit_depth = 8;
        png_significant_bits = 8;
        break;
    case IMAGE_FORMAT_XRGB2101010:
    case IMAGE_FORMAT_XBGR2101010:
        png_bit_depth = 16;
        png_significant_bits = 10;
        break;
    default:
        REPORT_UNHANDLED("image format", "%x", image->format);
    }
    png_set_IHDR(
        png_data,
        png_info,
        image->width,
        image->height,
        png_bit_depth,
        PNG_COLOR_TYPE_RGB,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );

    png_color_8 sig_bits = {
        .red = png_significant_bits,
        .blue = png_significant_bits,
        .green = png_significant_bits,
    };
    png_set_sBIT(png_data, png_info, &sig_bits);

    png_write_info(png_data, png_info);

    // transform and write image
    TIMING_START(png_encode);

    png_bytepp row_ptrs = malloc(image->height * sizeof(png_bytep));
    uint32_t bytes_per_pixel = image_format_bytes_per_pixel(image->format);
    if (image->format == IMAGE_FORMAT_XRGB8888) {
        // little-endian causes it to be effectively BGRX
        png_set_filler(png_data, 0, PNG_FILLER_AFTER);
        png_set_bgr(png_data);

        for (uint32_t y = 0; y < image->height; y++) {
            row_ptrs[y] = (png_bytep)&image->data[y * image->stride];
        }

        png_write_image(png_data, row_ptrs);
    } else if (image->format == IMAGE_FORMAT_XRGB2101010 ||
               image->format == IMAGE_FORMAT_XBGR2101010) {
        // this needs transcoding anyway, so use the PNG pixel format directly
        uint16_t *transcoded_data = malloc(image->width * image->height * 6);
        uint16_t *current_pixel = transcoded_data;
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

        for (uint32_t y = 0; y < image->height; y++) {
            row_ptrs[y] = (png_bytep)&transcoded_data[y * image->width];
        }

        // endianness, yay!
        png_set_swap(png_data);
        if (image->format == IMAGE_FORMAT_XRGB2101010) {
            png_set_bgr(png_data);
        }

        png_write_image(png_data, row_ptrs);
        free(transcoded_data);
    } else {
        REPORT_UNHANDLED("image format", "%x", image->format);
    }
    free(row_ptrs);
    png_write_end(png_data, png_info);

    TIMING_END(png_encode);

    png_destroy_write_struct(&png_data, &png_info);

    return result;
}

void image_destroy(Image *image) {
    free(image->data);
    free(image);
}
