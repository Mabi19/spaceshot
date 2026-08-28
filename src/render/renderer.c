#include "render/renderer.h"

const Renderer *renderer_get_default() {
    static const Renderer *result = NULL;
    if (result == NULL) {
        // TODO: choose between renderers when there is more than one
        result = &renderer_cairo;
        // The cairo renderer cannot fail to initialize.
        // Right now it's the only renderer, but will also be the fallback when
        // EGL fails to initialize
        result->init();
    }
    return result;
}
