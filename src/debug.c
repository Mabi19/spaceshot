#include "debug.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>

DebugMode debug_mode;

static const char *const MODE_NAMES[] = {
    [DEBUG_MODE_NONE] = "none",
    [DEBUG_MODE_CLIPPING] = "clipping",
    [DEBUG_MODE_SMART_BORDER] = "smart-border",
};
static const int MODE_COUNT = sizeof(MODE_NAMES) / sizeof(MODE_NAMES[0]);

void init_debug_mode() {
    const char *env = getenv("SPACESHOT_DEBUG");
    if (!env || env[0] == '\0') {
        debug_mode = DEBUG_MODE_NONE;
        return;
    }

    for (int i = 0; i < MODE_COUNT; i++) {
        if (strcmp(env, MODE_NAMES[i]) == 0) {
            debug_mode = i;
            return;
        }
    }

    report_warning("unknown debug mode %s\n", env);
}
