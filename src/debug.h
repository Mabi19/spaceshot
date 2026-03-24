#pragma once

typedef enum {
    DEBUG_MODE_NONE,
    DEBUG_MODE_CLIPPING,
    DEBUG_MODE_SMART_BORDER,
} DebugMode;

extern DebugMode debug_mode;
