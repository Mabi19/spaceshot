#include "label.h"
#include "image.h"
#include "render/command.h"
#include "ui/context.h"
#include <cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>

static void widget_label_layout(UIContext *ctx, WidgetHeader *header) {
    WidgetLabel *label = (WidgetLabel *)header;
    double scale = ctx->scale;

    double effective_font_size = label->font_size * scale;
    if (label->stale || label->effective_font_size != effective_font_size) {
        label->stale = false;
        label->effective_font_size = effective_font_size;
        pango_font_description_set_absolute_size(
            label->pango_font_description, effective_font_size * PANGO_SCALE
        );
        pango_layout_set_font_description(
            label->pango_layout, label->pango_font_description
        );
        if (label->max_width > 0) {
            pango_layout_set_width(
                label->pango_layout, label->max_width * scale * PANGO_SCALE
            );
        } else {
            pango_layout_set_width(label->pango_layout, -1);
        }

        PangoRectangle ink_rect, logical_rect;
        pango_layout_get_extents(label->pango_layout, &ink_rect, &logical_rect);
        header->bounds.width =
            ceil((double)logical_rect.width / PANGO_SCALE / scale);
        header->bounds.height =
            ceil((double)logical_rect.height / PANGO_SCALE / scale);

        label->x_offset = (double)ink_rect.x / PANGO_SCALE;
        label->y_offset = (double)ink_rect.y / PANGO_SCALE;
        // round up
        uint32_t canvas_width =
            (ink_rect.width + PANGO_SCALE - 1) / PANGO_SCALE;
        uint32_t canvas_height =
            (ink_rect.height + PANGO_SCALE - 1) / PANGO_SCALE;

        if (!label->rendered || label->rendered->width != canvas_width ||
            label->rendered->height != canvas_height) {
            // the size of the canvas changed
            if (label->rendered) {
                image_destroy(label->rendered);
            }
            label->rendered =
                image_new(canvas_width, canvas_height, IMAGE_FORMAT_ARGB8888);
        }
        // Clear the canvas. This is simpler than doing it through Cairo
        memset(
            label->rendered->data,
            0,
            (size_t)label->rendered->stride * canvas_height
        );
        cairo_surface_t *cairo_surface =
            image_make_cairo_surface(label->rendered);
        cairo_t *cr = cairo_create(cairo_surface);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);

        cairo_translate(cr, -label->x_offset, -label->y_offset);
        pango_cairo_show_layout(cr, label->pango_layout);

        cairo_destroy(cr);
        cairo_surface_destroy(cairo_surface);

        if (label->texture) {
            ctx->renderer->texture_destroy(label->texture);
        }
        label->texture = ctx->renderer->texture_new_from_image(label->rendered);

        ui_context_queue_draw(ctx);
    }
}

static void widget_label_draw(
    UIContext *ctx,
    WidgetHeader *header,
    double sx,
    double sy,
    RenderDisplayList *dl
) {
    WidgetLabel *label = (WidgetLabel *)header;

    RENDER_RECT(
        *dl,
        .bounds =
            (BBox){
                .x = round(
                    (header->bounds.x + sx) * ctx->scale + label->x_offset
                ),
                .y = round(
                    (header->bounds.y + sy) * ctx->scale + label->y_offset
                ),
                .width = label->rendered->width,
                .height = label->rendered->height,
            },
        .color = label->color,
        .texture = label->texture,
        .uv = RENDER_UV_DEFAULT
    );
}

const WidgetVTable WIDGET_LABEL_VTABLE = {
    .layout = widget_label_layout,
    .draw = widget_label_draw,
};

void widget_label_init(UIContext *ctx, WidgetLabel *label) {
    label->header.vtable = &WIDGET_LABEL_VTABLE;
    label->color = RENDER_COLOR_DEFAULT;
    label->pango_font_description = pango_font_description_new();
    label->pango_layout = pango_layout_new(ctx->pango_context);

    pango_layout_set_ellipsize(label->pango_layout, PANGO_ELLIPSIZE_END);
    // Currently, we only ever render text in single lines.
    // Set these two to be safe.
    pango_layout_set_single_paragraph_mode(label->pango_layout, true);
    pango_layout_set_wrap(label->pango_layout, PANGO_WRAP_NONE);
}

void widget_label_deinit(UIContext * /* ctx */, WidgetLabel *label) {
    g_object_unref(label->pango_layout);
    pango_font_description_free(label->pango_font_description);
}

void widget_label_set_content(
    UIContext *context, WidgetLabel *label, const char *content, int length
) {
    pango_layout_set_text(
        label->pango_layout, content, length < 0 ? -1 : length
    );
    label->stale = true;
    ui_context_queue_layout(context);
}

void widget_label_set_font(
    UIContext *context,
    WidgetLabel *label,
    const char *font_family,
    double font_size
) {
    pango_font_description_set_family_static(
        label->pango_font_description, font_family
    );
    label->font_size = font_size;
    label->stale = true;
    ui_context_queue_layout(context);
}

void widget_label_set_max_width(
    UIContext *context, WidgetLabel *label, double max_width
) {
    if (label->max_width != max_width) {
        label->max_width = max_width;
        label->stale = true;
        ui_context_queue_layout(context);
    }
}

void widget_label_set_color(
    UIContext *context, WidgetLabel *label, RenderColor color
) {
    label->color = color;
    label->stale = true;
    ui_context_queue_layout(context);
}
