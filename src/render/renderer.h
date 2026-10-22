#pragma once
#include "image.h"
#include "render/command.h"
#include "render/texture.h"
#include "wayland/dmabuf.h"
#include <wayland-client.h>

/** An opaque handle representing renderer-specific data for a surface. */
typedef struct RenderCanvas RenderCanvas;

typedef struct {
    double width;
    double height;
} RenderTextMetrics;

typedef enum {
    /**
     * The identity operator: returns a copy of the texture, same format.
     * Takes no extra parameters.
     */
    SHADER_PASS_IDENTITY,
} ShaderPassType;

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
    /**
     * Create a texture directly from a dmabuf, taking ownership of the dmabuf
     * in the process (as long as creation succeeds).
     * This is not a copy, so the dmabuf's contents may be filled in later
     * (in our case by the compositor with the screenshot)
     * and the texture will reflect that. Texture contents are undefined
     * until the dmabuf is filled in.
     * Returns NULL if dmabuf import is unsupported or fails.
     */
    RenderTexture *(*texture_new_from_dmabuf)(DmabufBuffer *buffer);
    /**
     * With dmabuf captures the transform is only known after the capture has
     * happened. This function can be used to update a dmabuf-backed texture's
     * transform.
     */
    void (*texture_dmabuf_set_transform)(
        RenderTexture *texture, ImageTransform transform
    );
    void (*texture_destroy)(RenderTexture *texture);
    /**
     * Process a texture through a shader pass.
     * Returns the new texture.
     * This function is optional and may be set to NULL if the renderer
     * does not support shader passes.
     */
    RenderTexture *(*texture_run_pass)(
        const RenderTexture *source, ShaderPassType type, ...
    );
    /**
     * Read a texture's contents back to the CPU.
     * The result is in the displayed orientation (i.e. texture transforms are
     * already unapplied) and is owned by the caller.
     */
    Image *(*texture_download)(const RenderTexture *texture);
    /** Whether dmabuf import is supported by this renderer. */
    bool (*supports_dmabuf)();
    /**
     * Filter a list of DRM format modifiers for a format down to those which
     * can be imported as textures. The list is compacted in place and the
     * number of remaining modifiers is returned.
     */
    size_t (*intersect_dmabuf_modifiers)(
        uint32_t format, uint64_t *modifiers, size_t modifier_count
    );
} Renderer;

extern const Renderer renderer_cairo;
extern const Renderer renderer_gl;

/**
 * Get the currently active renderer.
 * If this is the first call to the function, a renderer is instantiated.
 */
const Renderer *renderer_get_default();

/**
 * Clean up the currently active renderer if one was initialized.
 * Call this when no more renderer usage is expected.
 */
void renderer_cleanup();
