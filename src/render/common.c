#include "render/common.h"
#include "render/command.h"
#include <math.h>

void renderer_update_pango_fontdesc(
    PangoFontDescription *fontdesc, RenderTextStyle style
) {
    pango_font_description_set_family(fontdesc, style.font_family);
    pango_font_description_set_absolute_size(
        fontdesc, style.font_size * PANGO_SCALE
    );
    pango_font_description_set_weight(fontdesc, style.weight);
    pango_font_description_set_style(
        fontdesc, style.italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL
    );
}

void renderer_sanitize_rect(RenderCommandRect *rect) {
    BBox bounds = rect->bounds;
    RenderBorderRadius *radii = &rect->border_radius;

    // Constrain border radii so the rounded corners don't become bigger than
    // the box. Every corner is allowed to have a radius of at most half the
    // shorter side length. You can definitely have a more precise way of doing
    // this, but this is good enough.
    double limit =
        (bounds.width < bounds.height ? bounds.width : bounds.height) / 2.0;
    radii->tl = fmin(radii->tl, limit);
    radii->tr = fmin(radii->tr, limit);
    radii->bl = fmin(radii->bl, limit);
    radii->br = fmin(radii->br, limit);

    // Limit the border width to the corresponding rectangle dimension
    RenderBorderWidth *bwidth = &rect->border_width;
    double width = bounds.width;
    double height = bounds.height;
    if (bwidth->left + bwidth->right > width) {
        double shrink_factor = width / (bwidth->left + bwidth->right);
        bwidth->left *= shrink_factor;
        bwidth->right *= shrink_factor;
    }
    if (bwidth->top + bwidth->bottom > height) {
        double shrink_factor = height / (bwidth->top + bwidth->bottom);
        bwidth->top *= shrink_factor;
        bwidth->bottom *= shrink_factor;
    }
}
