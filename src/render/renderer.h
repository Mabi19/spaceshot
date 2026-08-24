#pragma once
#include "image.h"
#include "render/command.h"
#include "render/texture.h"
#include <wayland-client.h>

/** An opaque handle representing renderer-specific data for a surface. */
typedef struct RenderCanvas RenderCanvas;

/** A vtable of renderer operations. */
typedef struct {
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

    RenderTexture *(*texture_new_from_image)(const Image *image);
    void (*texture_destroy)(RenderTexture *texture);
} Renderer;

extern const Renderer renderer_cairo;
