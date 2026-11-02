#pragma once
#include "render/texture.h"
#include "ui/widget.h"

typedef struct {
    WidgetHeader header;
    /** The font size as set by widget_label_set_font. */
    double font_size;
    /**
     * 0 or negative = no limit.
     * Past this width the string is ellipsized
     */
    double max_width;
    RenderColor color;

    /** The effective font size (multiplied by scale). */
    double effective_font_size;
    /** The X and Y offsets of the ink box, in device pixels. */
    double x_offset;
    double y_offset;
    /** Whether the label needs to be redrawn. */
    bool stale;

    // The FontMap and Context live within the UIContext.
    PangoLayout *pango_layout;
    PangoFontDescription *pango_font_description;
    Image *rendered;
    RenderTexture *texture;
} WidgetLabel;

extern const WidgetVTable WIDGET_LABEL_VTABLE;

void widget_label_init(UIContext *ctx, WidgetLabel *label);
void widget_label_destroy(UIContext *ctx, WidgetLabel *label);
/**
 * Set the label's content.
 * The string is borrowed by the label and must outlive it.
 * Set length to a negative number to deduce it via strlen.
 */
void widget_label_set_content(
    UIContext *context, WidgetLabel *label, const char *content, int length
);
/**
 * Set the label's font family and size.
 * The font family is borrowed by the label and must outlive it.
 */
void widget_label_set_font(
    UIContext *context,
    WidgetLabel *label,
    const char *font_family,
    double font_size
);
/**
 * Set the label's max width.
 */
void widget_label_set_max_width(
    UIContext *context, WidgetLabel *label, double max_width
);
/**
 * Set the label's text color.
 */
void widget_label_set_color(
    UIContext *context, WidgetLabel *label, RenderColor color
);
