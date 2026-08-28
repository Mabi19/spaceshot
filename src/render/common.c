#include "render/common.h"

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
