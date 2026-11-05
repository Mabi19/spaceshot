#pragma once
#include "ui/label.h"

/**
 * A helper widget that lays out a label over a background rectangle.
 * Manipulate the label directly.
 */
typedef struct {
    WidgetHeader header;
    WidgetLabel label;
} WidgetTooltip;

extern const WidgetVTable WIDGET_TOOLTIP_VTABLE;

void widget_tooltip_init(UIContext *ctx, WidgetTooltip *tooltip);
void widget_tooltip_deinit(UIContext *ctx, WidgetTooltip *tooltip);
