#pragma once
#include "bbox.h"
#include "config/config.h"
#include "pango/pango-font.h"
#include "render/texture.h"

/** A 4-float color with premultiplied alpha (unlike ConfigColor) */
typedef struct {
    float r;
    float g;
    float b;
    float a;
} RenderColor;

static inline RenderColor config_color_to_render_color(ConfigColor src) {
    return (RenderColor){src.r * src.a, src.g * src.a, src.b * src.a, src.a};
}

typedef enum {
    RENDER_COMMAND_RECT,
    RENDER_COMMAND_TEXT,
} RenderCommandType;

typedef struct RenderCommand {
    struct RenderCommand *next;
    RenderCommandType type;
} RenderCommand;

typedef struct {
    RenderCommand header;
    /** In device pixels. Anything outside the surface is clipped. */
    BBox bounds;
    // TODO: radius
    RenderColor color;
    RenderTexture *texture;
    // TODO: uv (common modes: normal, screenspace, but clay also emits
    // arbitrary UVs) and indexing into atlases may be used by the UI lowerer
    // later
} RenderCommandRect;

/**
 * Declare the variables necessary for the RENDER_X macros to work.
 * The supplied LinkBuffer will be used for allocating render commands.
 * When done, the head of the display list can be extracted using
 * RENDER_DISPLAY_LIST_FINISH.
 */
#define RENDER_DISPLAY_LIST(arena)                                             \
    RenderCommand *cmd_first = NULL, *cmd_last = NULL;                         \
    LinkBuffer *cmd_arena = (arena)
#define RENDER_DISPLAY_LIST_FINISH cmd_first

/**
 * Push a rectangle to the display list. Only the bounds have to be specified.
 * If a texture is supplied, the color instead tints the texture.
 */
#define RENDER_RECT(...)                                                       \
    do {                                                                       \
        RenderCommandRect *cmd_rect = link_buffer_alloc(                       \
            cmd_arena, sizeof(RenderCommandRect), alignof(RenderCommandRect)   \
        );                                                                     \
        if (cmd_last)                                                          \
            cmd_last->next = &cmd_rect->header;                                \
        if (!cmd_first)                                                        \
            cmd_first = &cmd_rect->header;                                     \
        *cmd_rect = (RenderCommandRect){                                       \
            .header =                                                          \
                {                                                              \
                    .type = RENDER_COMMAND_RECT,                               \
                    .next = NULL,                                              \
                },                                                             \
            .color = (RenderColor){1, 1, 1, 1},                                \
            .texture = NULL,                                                   \
            __VA_ARGS__                                                        \
        };                                                                     \
        cmd_last = &cmd_rect->header;                                          \
    } while (0)

typedef struct {
    RenderCommand header;
    float x;
    float y;
    PangoFontDescription *font;
    char *content;
} RenderCommandText;

// TODO: push/pop clip rectangles
