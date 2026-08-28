#include "render/renderer.h"

static const Renderer *renderer;

const Renderer *renderer_get_default() {
    if (renderer == NULL) {
        // TODO: choose between renderers when there is more than one
        renderer = &renderer_cairo;
        // The cairo renderer cannot fail to initialize.
        // Right now it's the only renderer, but will also be the fallback when
        // EGL fails to initialize
        renderer->init();
    }
    return renderer;
}

void renderer_cleanup() {
    if (renderer) {
        renderer->cleanup();
    }
}
