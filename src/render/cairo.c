#include "image.h"
#include "log.h"
#include "render/command.h"
#include "render/renderer.h"
#include "render/texture.h"
#include "wayland/shared-memory.h"
#include <assert.h>
#include <cairo.h>
#include <stdlib.h>

typedef struct {
    SharedBuffer *shm;
    cairo_surface_t *cairo_surface;
    cairo_t *cr;
    bool is_busy;
} CairoBuffer;

constexpr size_t CAIRO_CANVAS_BUFFER_COUNT = 2;

typedef struct {
    struct wl_surface *wl_surface;
    CairoBuffer *buffers[CAIRO_CANVAS_BUFFER_COUNT];
    uint32_t device_width;
    uint32_t device_height;
    ImageFormat pixel_format;
} CairoCanvas;

static void
buffer_handle_release(void *data, struct wl_buffer * /* wl_buffer */) {
    CairoBuffer *buffer = data;
    buffer->is_busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_handle_release,
};

static CairoBuffer *
cairo_buffer_new(uint32_t width, uint32_t height, ImageFormat format) {
    CairoBuffer *result = calloc(1, sizeof(CairoBuffer));

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

static void cairo_buffer_attach_to_surface(
    CairoBuffer *buffer, struct wl_surface *surface
) {
    assert(!buffer->is_busy);
    wl_surface_attach(surface, buffer->shm->wl_buffer, 0, 0);
    buffer->is_busy = true;
}

void cairo_buffer_destroy(CairoBuffer *buffer) {
    cairo_destroy(buffer->cr);
    cairo_surface_destroy(buffer->cairo_surface);
    shared_buffer_destroy(buffer->shm);
    free(buffer);
}

static CairoBuffer *get_unused_buffer(CairoCanvas *canvas) {
    // first, try to get an existing buffer
    for (size_t i = 0; i < CAIRO_CANVAS_BUFFER_COUNT; i++) {
        if (!canvas->buffers[i]) {
            continue;
        }
        CairoBuffer *test_buf = canvas->buffers[i];
        if (!test_buf->is_busy &&
            test_buf->shm->width == canvas->device_width &&
            test_buf->shm->height == canvas->device_height) {
            return test_buf;
        }
    }

    // second, try to create a new one in an empty spot
    // or overwrite one with the wrong size
    for (size_t i = 0; i < CAIRO_CANVAS_BUFFER_COUNT; i++) {
        if (canvas->buffers[i]) {
            if (canvas->buffers[i]->shm->width == canvas->device_width &&
                canvas->buffers[i]->shm->height == canvas->device_height) {
                continue;
            }
            log_debug("destroyed buffer #%zu\n", i);
            cairo_buffer_destroy(canvas->buffers[i]);
        }
        canvas->buffers[i] = cairo_buffer_new(
            canvas->device_width, canvas->device_height, canvas->pixel_format
        );
        log_debug("created buffer #%zu\n", i);
        return canvas->buffers[i];
    }

    // last resort: overwrite the first one
    if (canvas->buffers[0]) {
        cairo_buffer_destroy(canvas->buffers[0]);
    }
    canvas->buffers[0] = cairo_buffer_new(
        canvas->device_width, canvas->device_height, canvas->pixel_format
    );
    log_debug("overwrote buffer #0 (last resort)\n");

    return canvas->buffers[0];
}

static RenderCanvas *cairo_canvas_new(
    struct wl_surface *surface,
    uint32_t device_width,
    uint32_t device_height,
    ImageFormat pixel_format
) {
    CairoCanvas *result = calloc(1, sizeof(CairoCanvas));
    result->wl_surface = surface;
    result->device_width = device_width;
    result->device_height = device_height;
    result->pixel_format = pixel_format;
    return (RenderCanvas *)result;
}

static void cairo_canvas_resize(
    RenderCanvas *render_canvas, uint32_t device_width, uint32_t device_height
) {
    CairoCanvas *canvas = (CairoCanvas *)render_canvas;
    canvas->device_width = device_width;
    canvas->device_height = device_height;
    // buffers are invalidated when acquired in get_unused_buffer
}

static void cairo_canvas_destroy(RenderCanvas *render_canvas) {
    CairoCanvas *canvas = (CairoCanvas *)render_canvas;

    for (size_t i = 0; i < CAIRO_CANVAS_BUFFER_COUNT; i++) {
        if (canvas->buffers[i]) {
            cairo_buffer_destroy(canvas->buffers[i]);
        }
    }

    free(canvas);
}

static void cairo_set_source_render_color(
    cairo_t *cr, RenderColor color, ImageFormat surface_format
) {
    float r, g, b;
    if (color.a == 0) {
        r = g = b = 0;
    } else {
        r = color.r / color.a;
        g = color.g / color.a;
        b = color.b / color.a;
    }
    // cairo only supports RGB order, so trick it if necessary
    if (surface_format & IMAGE_FORMAT_FLIPPED_ORDER) {
        cairo_set_source_rgba(cr, b, g, r, color.a);
    } else {
        cairo_set_source_rgba(cr, r, g, b, color.a);
    }
}

static void
cairo_draw(RenderCanvas *render_canvas, const RenderDisplayList dl) {
    CairoCanvas *canvas = (CairoCanvas *)render_canvas;
    CairoBuffer *draw_buf = get_unused_buffer(canvas);
    cairo_t *cr = draw_buf->cr;

    TIMING_START(cairo_frame);

    RenderCommand *cmd = dl.first;
    while (cmd != NULL) {
        switch (cmd->type) {
        case RENDER_COMMAND_RECT: {
            RenderCommandRect *rect = (RenderCommandRect *)cmd;
            if (rect->color.a > 0) {
                // TODO: border radii (make a helper?)
                cairo_rectangle(
                    cr,
                    rect->bounds.x,
                    rect->bounds.y,
                    rect->bounds.width,
                    rect->bounds.height
                );

                if (rect->texture) {
                    // TODO: UVs (set the pattern matrix)
                    // TODO: tinting (tricky. Clay can just tint anything, so we
                    // will need it eventually)
                    cairo_set_source(cr, (cairo_pattern_t *)rect->texture);
                    cairo_fill(cr);
                } else {
                    cairo_set_source_render_color(
                        cr, rect->color, canvas->pixel_format
                    );
                    cairo_fill(cr);
                }
            }
            // TODO: border

            break;
        }
        default:
            REPORT_UNHANDLED("render command type", "%d", cmd->type);
        }
        cmd = cmd->next;
    }

    cairo_surface_flush(draw_buf->cairo_surface);

    TIMING_END(cairo_frame);

    cairo_buffer_attach_to_surface(draw_buf, canvas->wl_surface);
    // Finishing a frame with GL damages commits, so also do it here for
    // consistency.
    wl_surface_damage_buffer(
        canvas->wl_surface, 0, 0, canvas->device_width, canvas->device_height
    );
    wl_surface_commit(canvas->wl_surface);
}

// For the cairo backend, textures are cairo_pattern_t.
static RenderTexture *cairo_texture_new_from_image(const Image *image) {
    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        image->data,
        image_format_to_cairo(image->format),
        image->width,
        image->height,
        image->stride
    );
    cairo_pattern_t *pattern = cairo_pattern_create_for_surface(surface);
    // The pattern takes a reference on the surface, and we don't need the
    // surface anymore, so we can relinquish our ownership.
    cairo_surface_destroy(surface);
    return (RenderTexture *)pattern;
}

static void cairo_texture_destroy(RenderTexture *render_texture) {
    cairo_pattern_t *pattern = (cairo_pattern_t *)render_texture;
    cairo_pattern_destroy(pattern);
}

const Renderer renderer_cairo = {
    .canvas_new = cairo_canvas_new,
    .canvas_resize = cairo_canvas_resize,
    .canvas_destroy = cairo_canvas_destroy,
    .draw = cairo_draw,
    .texture_new_from_image = cairo_texture_new_from_image,
    .texture_destroy = cairo_texture_destroy,
};
