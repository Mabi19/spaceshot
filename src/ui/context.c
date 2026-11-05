#include "context.h"
#include "log.h"
#include "render/renderer.h"
#include "ui/widget.h"
#include "wayland/overlay-surface.h"
#include <pango/pangocairo.h>

void ui_context_init_common(UIContextCommon *common) {
    common->renderer = renderer_get_default();
    common->pango_fontmap = pango_cairo_font_map_get_default();
    common->pango_context =
        pango_font_map_create_context(common->pango_fontmap);
}

void ui_context_init(UIContext *ctx, UIContextCommon *common) {
    ctx->renderer = common->renderer;
    ctx->pango_context = common->pango_context;
    ctx->pango_fontmap = common->pango_fontmap;

    ctx->picker_root = NULL;

    ctx->scale = 1.0;
    ctx->layout_queued = false;
    ctx->draw_queued = false;
}

void ui_context_queue_layout(UIContext *ctx) { ctx->layout_queued = true; }
void ui_context_queue_draw(UIContext *ctx) { ctx->draw_queued = true; }

void ui_context_dispatch_queued(UIContext *ctx, OverlaySurface *surface) {
    if (ctx->layout_queued) {
        if (ctx->picker_root) {
            log_debug("laying out picker_root\n");
            widget_layout(ctx, ctx->picker_root);
        }
        ctx->layout_queued = false;
    }
    if (ctx->draw_queued) {
        overlay_surface_queue_draw(surface);
        ctx->draw_queued = false;
    }
}
