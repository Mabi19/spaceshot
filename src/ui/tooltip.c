#include "tooltip.h"
#include "render/command.h"

constexpr double TOOLTIP_PADDING_X = 6;
constexpr double TOOLTIP_PADDING_Y = 4;

static void widget_tooltip_layout(UIContext *ctx, WidgetHeader *header) {
    WidgetTooltip *tooltip = (WidgetTooltip *)header;

    widget_layout(ctx, &tooltip->label.header);
    BBox *label_bounds = &tooltip->label.header.bounds;
    label_bounds->x = TOOLTIP_PADDING_X;
    label_bounds->y = TOOLTIP_PADDING_Y;
    tooltip->header.bounds.width = label_bounds->width + TOOLTIP_PADDING_X * 2;
    tooltip->header.bounds.height =
        label_bounds->height + TOOLTIP_PADDING_Y * 2;
}

static void widget_tooltip_draw(
    UIContext *ctx,
    WidgetHeader *header,
    double sx,
    double sy,
    RenderDisplayList *dl
) {
    WidgetTooltip *tooltip = (WidgetTooltip *)header;

    RENDER_RECT(
        *dl,
        .bounds =
            bbox_scale(bbox_translate(header->bounds, sx, sy), ctx->scale),
        .color = {0, 0, 0, 0.75},
        .border_radius = RENDER_BORDER_RADIUS(4 * ctx->scale)
    );
    widget_draw(ctx, &tooltip->label.header, sx, sy, dl);
}

const WidgetVTable WIDGET_TOOLTIP_VTABLE = {
    .layout = widget_tooltip_layout,
    .draw = widget_tooltip_draw,
};

void widget_tooltip_init(UIContext *ctx, WidgetTooltip *tooltip) {
    tooltip->header.vtable = &WIDGET_TOOLTIP_VTABLE;
    widget_label_init(ctx, &tooltip->label);
}
void widget_tooltip_deinit(UIContext *ctx, WidgetTooltip *tooltip) {
    widget_label_deinit(ctx, &tooltip->label);
}
