#include "render/renderer.h"
#include "log.h"
#include <string.h>

static const Renderer *renderer;

static bool renderer_done = false;

const Renderer *renderer_get_default() {
    if (renderer) {
        return renderer;
    }
    if (renderer_done) {
        REPORT_ERROR_INTERNAL(
            "Attempted to get renderer after renderer_cleanup() was called"
        );
    }

    const char *env_var = getenv("SPACESHOT_RENDERER");
    if (env_var != NULL) {
        if (strcmp(env_var, "gl") == 0) {
            if (renderer_gl.init()) {
                renderer = &renderer_gl;
            }
        } else if (strcmp(env_var, "cairo") == 0) {
            renderer_cairo.init();
            renderer = &renderer_cairo;
        }
        goto end;
    }

    auto renderers = config_get()->renderer;
    for (size_t i = 0; i < renderers.count; i++) {
        switch (renderers.items[i]) {
        case CONFIG_RENDERER_ITEM_GL:
            log_debug("trying renderer gl...\n");
            if (renderer_gl.init()) {
                renderer = &renderer_gl;
                goto end;
            }
            break;
        case CONFIG_RENDERER_ITEM_CAIRO:
            log_debug("trying renderer cairo...\n");
            renderer_cairo.init();
            renderer = &renderer_cairo;
            goto end;
        default:
            REPORT_UNHANDLED("renderer", "%d", renderers.items[i]);
        }
    }

end:

    if (renderer == NULL) {
        report_error_fatal(
            "Couldn't choose renderer, try adding 'cairo' to the renderer "
            "config option\n"
        );
    }
    return renderer;
}

void renderer_cleanup() {
    if (renderer) {
        renderer->cleanup();
        renderer = NULL;
        renderer_done = true;
    }
}
