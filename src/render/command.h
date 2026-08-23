#pragma once
#include "bbox.h"
#include "config/config.h"
#include "link-buffer.h"
#include "render/texture.h"

/** A 4-float color with premultiplied alpha (unlike ConfigColor) */
typedef struct {
    float r;
    float g;
    float b;
    float a;
} RenderColor;

constexpr RenderColor RENDER_COLOR_DEFAULT = {1, 1, 1, 1};

static inline RenderColor config_color_to_render_color(ConfigColor src) {
    return (RenderColor){src.r * src.a, src.g * src.a, src.b * src.a, src.a};
}

typedef enum {
    RENDER_COMMAND_RECT,
} RenderCommandType;

typedef struct RenderCommand {
    struct RenderCommand *next;
    RenderCommandType type;
} RenderCommand;

typedef struct {
    float tl;
    float tr;
    float bl;
    float br;
} RenderRectRadius;

typedef struct {
    float left;
    float right;
    float top;
    float bottom;
} RenderBorderWidth;

/**
 * (u, v)0 = top left corner, (u, v)1 = bottom right corner.
 * The RENDER_UV_DEFAULT constant is available for the common case of "whole
 * texture".
 */
typedef struct {
    float u0;
    float v0;
    float u1;
    float v1;
} RenderUV;

constexpr RenderUV RENDER_UV_DEFAULT = {0, 0, 1, 1};

typedef struct {
    RenderCommand header;
    /** In device pixels. Anything outside the surface is clipped. */
    BBox bounds;
    /**
     * If texture is unset, the color of the rectangle.
     * Otherwise, a tint to apply to the rectangle.
     * Must be set (unless you want to render nothing)
     */
    RenderColor color;
    RenderRectRadius border_radius;
    RenderBorderWidth border_width;
    RenderColor border_color;
    RenderTexture *texture;
    /** Must be set if the texture is set. */
    RenderUV uv;
} RenderCommandRect;

typedef struct {
    LinkBuffer *arena;
    RenderCommand *first;
    RenderCommand *last;
} RenderDisplayList;

/**
 * Push a rectangle to the display list. Only the bounds have to be specified.
 * If a texture is supplied, the color instead tints the texture.
 */
#define RENDER_RECT(dl, ...)                                                   \
    do {                                                                       \
        RenderCommandRect *cmd_rect = link_buffer_alloc(                       \
            (dl).arena, sizeof(RenderCommandRect), alignof(RenderCommandRect)  \
        );                                                                     \
        if ((dl).last)                                                         \
            (dl).last->next = &cmd_rect->header;                               \
        if (!(dl).first)                                                       \
            (dl).first = &cmd_rect->header;                                    \
        *cmd_rect = (RenderCommandRect){                                       \
            .header =                                                          \
                {                                                              \
                    .type = RENDER_COMMAND_RECT,                               \
                    .next = NULL,                                              \
                },                                                             \
            __VA_ARGS__                                                        \
        };                                                                     \
        (dl).last = &cmd_rect->header;                                         \
    } while (0)

// TODO: push/pop clip rectangles
