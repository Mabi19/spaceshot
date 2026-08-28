#include "image.h"
#include "log.h"
#include "render/command.h"
#include "render/common.h"
#include "render/renderer.h"
#include "render/texture.h"
#include "wayland/shared-memory.h"
#include <assert.h>
#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>
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

typedef struct {
    cairo_pattern_t *pattern;
    uint32_t width;
    uint32_t height;
} CairoTexture;

static void
buffer_handle_release(void *data, struct wl_buffer * /* wl_buffer */) {
    CairoBuffer *buffer = data;
    buffer->is_busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_handle_release,
};

static CairoBuffer *
buffer_new(uint32_t width, uint32_t height, ImageFormat format) {
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

static void
buffer_attach_to_surface(CairoBuffer *buffer, struct wl_surface *surface) {
    assert(!buffer->is_busy);
    wl_surface_attach(surface, buffer->shm->wl_buffer, 0, 0);
    buffer->is_busy = true;
}

static void buffer_destroy(CairoBuffer *buffer) {
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
            buffer_destroy(canvas->buffers[i]);
        }
        canvas->buffers[i] = buffer_new(
            canvas->device_width, canvas->device_height, canvas->pixel_format
        );
        log_debug("created buffer #%zu\n", i);
        return canvas->buffers[i];
    }

    // last resort: overwrite the first one
    if (canvas->buffers[0]) {
        buffer_destroy(canvas->buffers[0]);
    }
    canvas->buffers[0] = buffer_new(
        canvas->device_width, canvas->device_height, canvas->pixel_format
    );
    log_debug("overwrote buffer #0 (last resort)\n");

    return canvas->buffers[0];
}

// internal state: Pango objects necessary for text rendering.
static PangoContext *pango_context;
static PangoLayout *pango_layout;
static PangoFontDescription *pango_font_description;

static bool renderer_cairo_init() {
    PangoFontMap *fontmap = pango_cairo_font_map_get_default();
    pango_context = pango_font_map_create_context(fontmap);
    pango_layout = pango_layout_new(pango_context);
    pango_font_description = pango_font_description_new();

    return true;
}

static void renderer_cairo_cleanup() {
    g_object_unref(pango_context);
    g_object_unref(pango_layout);
    pango_font_description_free(pango_font_description);
}

static RenderCanvas *renderer_cairo_canvas_new(
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

static void renderer_cairo_canvas_resize(
    RenderCanvas *render_canvas, uint32_t device_width, uint32_t device_height
) {
    CairoCanvas *canvas = (CairoCanvas *)render_canvas;
    canvas->device_width = device_width;
    canvas->device_height = device_height;
    // buffers are invalidated when acquired in get_unused_buffer
}

static void renderer_cairo_canvas_destroy(RenderCanvas *render_canvas) {
    CairoCanvas *canvas = (CairoCanvas *)render_canvas;

    for (size_t i = 0; i < CAIRO_CANVAS_BUFFER_COUNT; i++) {
        if (canvas->buffers[i]) {
            buffer_destroy(canvas->buffers[i]);
        }
    }

    free(canvas);
}

static void set_source_render_color(
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
make_rounded_rect_path(cairo_t *cr, BBox bounds, RenderBorderRadius radii) {
    constexpr double PI = 3.141592653589793115997963468544185161590576171875;
    constexpr double DEG = PI / 180.0;

    // Constrain border radii so the rounded corners don't become bigger than
    // the box. Every corner is allowed to have a radius of at most half the
    // shorter side length. You can definitely have a more precise way of doing
    // this, but this is good enough.
    double limit =
        (bounds.width < bounds.height ? bounds.width : bounds.height) / 2.0;
    radii.tl = fmin(radii.tl, limit);
    radii.tr = fmin(radii.tr, limit);
    radii.bl = fmin(radii.bl, limit);
    radii.br = fmin(radii.br, limit);

    if (radii.tl > 0) {
        cairo_new_sub_path(cr);
        cairo_arc(
            cr,
            bounds.x + radii.tl,
            bounds.y + radii.tl,
            radii.tl,
            -180 * DEG,
            -90 * DEG
        );
    } else {
        // this also starts a sub-path, so the close path below still works
        cairo_move_to(cr, bounds.x, bounds.y);
    }
    if (radii.tr > 0) {
        cairo_arc(
            cr,
            bounds.x + bounds.width - radii.tr,
            bounds.y + radii.tr,
            radii.tr,
            -90 * DEG,
            0 * DEG
        );
    } else {
        cairo_line_to(cr, bounds.x + bounds.width, bounds.y);
    }
    if (radii.br > 0) {
        cairo_arc(
            cr,
            bounds.x + bounds.width - radii.br,
            bounds.y + bounds.height - radii.br,
            radii.br,
            0 * DEG,
            90 * DEG
        );
    } else {
        cairo_line_to(cr, bounds.x + bounds.width, bounds.y + bounds.height);
    }
    if (radii.bl > 0) {
        cairo_arc(
            cr,
            bounds.x + radii.bl,
            bounds.y + bounds.height - radii.bl,
            radii.bl,
            90 * DEG,
            180 * DEG
        );
    } else {
        cairo_line_to(cr, bounds.x, bounds.y + bounds.height);
    }

    cairo_close_path(cr);
}

static void
renderer_cairo_draw(RenderCanvas *render_canvas, const RenderDisplayList dl) {
    CairoCanvas *canvas = (CairoCanvas *)render_canvas;
    CairoBuffer *draw_buf = get_unused_buffer(canvas);
    cairo_t *cr = draw_buf->cr;
    // Necessary for borders to draw properly, and doesn't impact anything else.
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);

    int clip_nesting_level = 0;
    bool has_updated_pango_context = false;

    TIMING_START(cairo_frame);

    RenderCommand *cmd = dl.first;
    while (cmd != NULL) {
        switch (cmd->type) {
        case RENDER_COMMAND_CLEAR: {
            RenderCommandClear *clear = (RenderCommandClear *)cmd;
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            set_source_render_color(cr, clear->color, canvas->pixel_format);
            cairo_paint(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
            break;
        }
        case RENDER_COMMAND_RECT: {
            RenderCommandRect *rect = (RenderCommandRect *)cmd;
            if (rect->color.a > 0) {
                assert(rect->bounds.width > 0 && rect->bounds.height > 0);
                make_rounded_rect_path(cr, rect->bounds, rect->border_radius);

                if (rect->texture) {
                    // UVs: set the pattern matrix
                    assert(
                        rect->uv.u1 > rect->uv.u0 && rect->uv.v1 > rect->uv.v0
                    );
                    CairoTexture *tex = (CairoTexture *)rect->texture;
                    double sx = (rect->uv.u1 - rect->uv.u0) * tex->width /
                                rect->bounds.width;
                    double sy = (rect->uv.v1 - rect->uv.v0) * tex->height /
                                rect->bounds.height;
                    cairo_matrix_t ctm;
                    cairo_matrix_init(
                        &ctm,
                        sx, // scale x
                        0,
                        0,
                        sy, // scale y
                        rect->uv.u0 * tex->width -
                            sx * rect->bounds.x, // translate x
                        rect->uv.v0 * tex->height -
                            sy * rect->bounds.y // translate y
                    );
                    cairo_pattern_set_matrix(tex->pattern, &ctm);

                    // Tint: We can't do a proper multiply in Cairo, so instead
                    // white means "render as-is" and for anything else the
                    // image is treated as an alpha mask.
                    RenderColor tint = rect->color;
                    if (tint.r != 1 || tint.g != 1 || tint.b != 1 ||
                        tint.a != 1) {
                        cairo_save(cr);
                        cairo_clip(cr);
                        set_source_render_color(cr, tint, canvas->pixel_format);
                        cairo_mask(cr, tex->pattern);
                        cairo_restore(cr);
                    } else {
                        // Tint is opaque white, which means no change. So we
                        // can just draw it directly
                        cairo_set_source(cr, tex->pattern);
                        cairo_fill(cr);
                    }
                } else {
                    set_source_render_color(
                        cr, rect->color, canvas->pixel_format
                    );
                    cairo_fill(cr);
                }
            }

            if (rect->border_color.a > 0) {
                // The outer edge of the border is the same as the regular
                // rectangle.
                make_rounded_rect_path(cr, rect->bounds, rect->border_radius);
                // The inner edge is inset.
                // Limit the border width to the corresponding rectangle
                // dimension
                RenderBorderWidth bwidth = rect->border_width;
                double width = rect->bounds.width;
                double height = rect->bounds.height;
                if (bwidth.left + bwidth.right > width) {
                    double shrink_factor = width / (bwidth.left + bwidth.right);
                    bwidth.left *= shrink_factor;
                    bwidth.right *= shrink_factor;
                }
                if (bwidth.top + bwidth.bottom > height) {
                    double shrink_factor =
                        height / (bwidth.top + bwidth.bottom);
                    bwidth.top *= shrink_factor;
                    bwidth.bottom *= shrink_factor;
                }
                // Compute the inner rectangle, where the border isn't.
                BBox inner_rect = rect->bounds;
                inner_rect.x += bwidth.left;
                inner_rect.width -= bwidth.left;
                inner_rect.width -= bwidth.right;
                inner_rect.y += bwidth.top;
                inner_rect.height -= bwidth.top;
                inner_rect.height -= bwidth.bottom;

                // Only poke a hole if there is a hole to be poked.
                // Float imprecision may have caused small errors that mean the
                // width is slightly above zero when logically the border should
                // cover the whole rect.
                if (inner_rect.width > 1e-6 && inner_rect.height > 1e-6) {
                    // To look good, inner radius + border width = outer radius.
                    // Here each corner touches two (potentially different)
                    // border widths; apply the reduction using the thicker
                    // border because overadjusting looks better than
                    // underadjusting.
                    RenderBorderRadius inner_radius = rect->border_radius;
                    inner_radius.tl = fmax(
                        inner_radius.tl - fmax(bwidth.top, bwidth.left), 0
                    );
                    inner_radius.tr = fmax(
                        inner_radius.tr - fmax(bwidth.top, bwidth.right), 0
                    );
                    inner_radius.bl = fmax(
                        inner_radius.bl - fmax(bwidth.bottom, bwidth.left), 0
                    );
                    inner_radius.br = fmax(
                        inner_radius.br - fmax(bwidth.bottom, bwidth.right), 0
                    );
                    make_rounded_rect_path(cr, inner_rect, inner_radius);

                    set_source_render_color(
                        cr, rect->border_color, canvas->pixel_format
                    );
                    cairo_fill(cr);
                }
            }

            break;
        }
        case RENDER_COMMAND_TEXT: {
            RenderCommandText *text = (RenderCommandText *)cmd;

            if (!has_updated_pango_context) {
                // We don't change the transformation or font properties, so
                // this is technically unnecessary, but what happens in Pango is
                // an implementation detail, so do it anyway.
                pango_cairo_update_context(cr, pango_context);
                pango_layout_context_changed(pango_layout);
                has_updated_pango_context = true;
            }

            renderer_update_pango_fontdesc(pango_font_description, text->style);
            pango_layout_set_text(pango_layout, text->content, text->length);
            pango_layout_set_font_description(
                pango_layout, pango_font_description
            );

            set_source_render_color(cr, text->color, canvas->pixel_format);
            cairo_move_to(cr, text->x, text->y);
            pango_cairo_show_layout(cr, pango_layout);
            break;
        }
        case RENDER_COMMAND_PUSH_CLIP: {
            RenderCommandPushClip *push_clip = (RenderCommandPushClip *)cmd;
            // pop clip will restore this
            cairo_save(cr);
            cairo_rectangle(
                cr,
                push_clip->bounds.x,
                push_clip->bounds.y,
                push_clip->bounds.width,
                push_clip->bounds.height
            );
            cairo_clip(cr);
            clip_nesting_level++;
            break;
        }
        case RENDER_COMMAND_POP_CLIP: {
            assert(clip_nesting_level > 0);
            cairo_restore(cr);
            break;
        }
        default:
            REPORT_UNHANDLED("render command type", "%d", cmd->type);
        }
        cmd = cmd->next;
    }

    assert(cairo_status(cr) == CAIRO_STATUS_SUCCESS);
    assert(clip_nesting_level == 0);
    cairo_surface_flush(draw_buf->cairo_surface);

    TIMING_END(cairo_frame);

    buffer_attach_to_surface(draw_buf, canvas->wl_surface);
    // Finishing a frame with GL damages & commits, so also do it here for
    // consistency.
    wl_surface_damage_buffer(
        canvas->wl_surface, 0, 0, canvas->device_width, canvas->device_height
    );
    wl_surface_commit(canvas->wl_surface);
}

static RenderTextMetrics renderer_cairo_measure_text(
    const char *content, int length, RenderTextStyle style
) {
    renderer_update_pango_fontdesc(pango_font_description, style);
    pango_layout_set_text(pango_layout, content, length);
    pango_layout_set_font_description(pango_layout, pango_font_description);

    PangoRectangle extents;
    pango_layout_get_pixel_extents(pango_layout, NULL, &extents);
    return (RenderTextMetrics){
        .width = extents.width,
        .height = extents.height,
    };
}

static RenderTexture *
renderer_cairo_texture_new_from_image(const Image *image) {
    CairoTexture *result = calloc(1, sizeof(CairoTexture));
    result->width = image->width;
    result->height = image->height;
    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        image->data,
        image_format_to_cairo(image->format),
        image->width,
        image->height,
        image->stride
    );
    result->pattern = cairo_pattern_create_for_surface(surface);
    // The pattern takes a reference on the surface, and we don't need the
    // surface anymore, so we can relinquish our ownership.
    cairo_surface_destroy(surface);
    return (RenderTexture *)result;
}

static void renderer_cairo_texture_destroy(RenderTexture *render_texture) {
    CairoTexture *texture = (CairoTexture *)render_texture;
    cairo_pattern_destroy(texture->pattern);
    free(texture);
}

const Renderer renderer_cairo = {
    .init = renderer_cairo_init,
    .cleanup = renderer_cairo_cleanup,
    .canvas_new = renderer_cairo_canvas_new,
    .canvas_resize = renderer_cairo_canvas_resize,
    .canvas_destroy = renderer_cairo_canvas_destroy,
    .draw = renderer_cairo_draw,
    .measure_text = renderer_cairo_measure_text,
    .texture_new_from_image = renderer_cairo_texture_new_from_image,
    .texture_destroy = renderer_cairo_texture_destroy,
};
