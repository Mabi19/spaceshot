#include "render/renderer.h"

static const Renderer *renderer;

const Renderer *renderer_get_default() {
    if (renderer == NULL) {
        // The GL renderer is preferred, but Cairo is a fallback for when EGL
        // fails to initialize (e.g. no GPU or unsupported platform).
        if (renderer_gl.init()) {
            renderer = &renderer_gl;
        } else {
            renderer = &renderer_cairo;
            // The cairo renderer cannot fail to initialize.
            renderer->init();
        }
    }
    return renderer;
}

void renderer_cleanup() {
    if (renderer) {
        renderer->cleanup();
        renderer = NULL;
    }
}
