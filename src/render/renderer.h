#pragma once
#include "image.h"
#include "render/command.h"
#include "render/texture.h"
#include <wayland-client.h>

/** An opaque handle representing renderer-specific data for a surface. */
typedef struct RenderCanvas RenderCanvas;

typedef struct {
    double width;
    double height;
} RenderTextMetrics;

/** A vtable of renderer operations. */
typedef struct {
    /** Initialize the renderer's internal state. */
    bool (*init)();
    /**
     * Clean up the renderer's internal state. After this none of the other
     * functions can be used anymore
     */
    void (*cleanup)();
    RenderCanvas *(*canvas_new)(
        struct wl_surface *wl_surface,
        uint32_t device_width,
        uint32_t device_height,
        ImageFormat format
    );
    void (*canvas_resize)(
        RenderCanvas *canvas, uint32_t device_width, uint32_t device_height
    );
    void (*canvas_destroy)(RenderCanvas *canvas);
    /** Draw a display list, then damage and commit the surface. */
    void (*draw)(RenderCanvas *canvas, const RenderDisplayList dl);
    /**
     * Measure a piece of text's width and height.
     * Similarly to the TEXT render command the string is length-based,
     * but you can set length to -1 to deduce it via strlen.
     */
    RenderTextMetrics (*measure_text)(
        const char *content, int length, RenderTextStyle style
    );

    RenderTexture *(*texture_new_from_image)(const Image *image);
    void (*texture_destroy)(RenderTexture *texture);
} Renderer;

extern const Renderer renderer_cairo;

/**
 * Get the currently active renderer.
 * If this is the first call to the function, a renderer is instantiated.
 */
const Renderer *renderer_get_default();
