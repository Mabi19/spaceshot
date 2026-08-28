#include "render/renderer.h"

const Renderer *renderer_get_default() {
    static const Renderer *result = NULL;
    if (result == NULL) {
        // TODO: choose between renderers when there is more than one
        result = &renderer_cairo;
        result->init();
    }
    return result;
}
