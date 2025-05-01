#include "wayland/render.h"
#include "image.h"
#include <assert.h>
#include <cairo.h>
#include <stdlib.h>

static void
buffer_handle_release(void *data, struct wl_buffer * /* wl_buffer */) {
    RenderBuffer *buffer = data;
    buffer->is_busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_handle_release,
};

RenderBuffer *
render_buffer_new(uint32_t width, uint32_t height, ImageFormat format) {
    RenderBuffer *result = calloc(1, sizeof(RenderBuffer));

    uint32_t stride =
        cairo_format_stride_for_width(image_format_to_cairo(format), width);
    result->shm =
        shared_buffer_new(width, height, stride, image_format_to_wl(format));
    wl_buffer_add_listener(result->shm->wl_buffer, &buffer_listener, result);

    result->cairo_surface = cairo_image_surface_create_for_data(
        result->shm->data, image_format_to_cairo(format), width, height, stride
    );
    result->cr = cairo_create(result->cairo_surface);

    return result;
}

void render_buffer_attach_to_surface(
    RenderBuffer *buffer, struct wl_surface *surface
) {
    assert(!buffer->is_busy);
    wl_surface_attach(surface, buffer->shm->wl_buffer, 0, 0);
    buffer->is_busy = true;
}

void render_buffer_destroy(RenderBuffer *buffer) {
    cairo_destroy(buffer->cr);
    cairo_surface_destroy(buffer->cairo_surface);
    shared_buffer_destroy(buffer->shm);
    free(buffer);
}

void cairo_set_source_config_color(
    cairo_t *cr, ConfigColor color, ImageFormat surface_format
) {
    // cairo only supports RGB order, so trick it if necessary
    if (surface_format & IMAGE_FORMAT_FLIPPED_ORDER) {
        cairo_set_source_rgba(cr, color.b, color.g, color.r, color.a);
    } else {
        cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
    }
}

int config_length_to_pixels(ConfigLength length, uint32_t surface_scale) {
    // length.unit is only pixels for now
    return round((length.value * surface_scale) / 120.0);
}
