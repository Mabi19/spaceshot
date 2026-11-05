#pragma once

typedef enum {
    DEBUG_MODE_NONE,
    DEBUG_MODE_NO_EXCLUSIVE_KEYBOARD,
    DEBUG_MODE_ALWAYS_SHOW_OUTPUT_PICKER,
} DebugMode;

extern DebugMode debug_mode;
