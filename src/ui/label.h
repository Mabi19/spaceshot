#pragma once
#include "render/texture.h"
#include "ui/widget.h"

typedef struct {
    WidgetHeader header;
    /** Borrowed. */
    const char *content;
    /**
     * Negative = string is null-terminated, use strlen.
     */
    int length;

    /**
     * This is expected to outlive the UI context
     * (either static string or config'd is fine)
     */
    const char *font_family;
    double font_size;
    /**
     * 0 or negative = no limit.
     * Past this width the string is ellipsized
     */
    double max_width;
    RenderColor color;
    int weight;
    bool italic;

    // TODO: PangoFontDescription and PangoLayout.
    // The FontMap and Context live within the UIContext.

    Image *rendered;
    RenderTexture *texture;
} WidgetLabel;

extern const WidgetVTable WIDGET_LABEL_VTABLE;
