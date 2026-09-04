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
    RENDER_COMMAND_CLEAR,
    RENDER_COMMAND_RECT,
    RENDER_COMMAND_TEXT,
    RENDER_COMMAND_PUSH_CLIP,
    RENDER_COMMAND_POP_CLIP,
} RenderCommandType;

typedef struct RenderCommand {
    struct RenderCommand *next;
    RenderCommandType type;
} RenderCommand;

/**
 * Note that a display list's commands MUST cover the whole surface
 * to not produce ghosting.
 */
typedef struct {
    LinkBuffer *arena;
    RenderCommand *first;
    RenderCommand *last;
} RenderDisplayList;

/** Set the entire canvas to a solid color. */
typedef struct {
    RenderCommand header;
    RenderColor color;
} RenderCommandClear;

typedef struct {
    float tl;
    float tr;
    float bl;
    float br;
} RenderBorderRadius;

#define RENDER_BORDER_RADIUS(r)                                                \
    (RenderBorderRadius) { (r), (r), (r), (r) }

typedef struct {
    float left;
    float right;
    float top;
    float bottom;
} RenderBorderWidth;

#define RENDER_BORDER_WIDTH(w)                                                 \
    (RenderBorderWidth) { (w), (w), (w), (w) }

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
     * The color of the rectangle.
     * If a texture is set and the color is non-white, it is tinted to this
     * color; note that due to Cairo limitations tinting will only work properly
     * on alpha-mask-like textures.
     * Must be set (unless you want to render nothing)
     */
    RenderColor color;
    RenderTexture *texture;
    RenderBorderRadius border_radius;
    /**
     * The border is inset to the rectangle (like CSS box-sizing: border-box).
     */
    RenderBorderWidth border_width;
    RenderColor border_color;
    /** Must be set if the texture is set. */
    RenderUV uv;
} RenderCommandRect;

typedef struct {
    /**
     * This is expected to outlive the renderer
     * (either static string or config'd is fine)
     */
    const char *font_family;
    float font_size;
    int weight;
    bool italic;
} RenderTextStyle;

/** Create a RenderTextStyle object with the default values at the specified
 * scale. */
#define RENDER_TEXT_STYLE_DEFAULT(scale)                                       \
    (RenderTextStyle) {                                                        \
        .font_family = "Sans", .font_size = 16.0 * (scale) / 120.0,            \
        .weight = 400, .italic = false                                         \
    }

typedef struct {
    RenderCommand header;
    float x;
    float y;
    /**
     * Not necessarily null-terminated: set length to -1 to determine length
     * via strlen.
     */
    const char *content;
    int length;
    RenderTextStyle style;
    /**
     * The color of the text.
     * Due to how the GL renderer currently works, emoji will also be tinted
     * this color; therefore, avoid emojis in non-white text.
     */
    RenderColor color;
    /** Used internally by the renderers, must be unset (set to NULL) */
    void *renderer_data;
} RenderCommandText;

/** Different from BBox because clipping is always in device coordinates. */
typedef struct {
    int x;
    int y;
    int width;
    int height;
} RenderClipRect;

/**
 * Intersect a box with the clipping region.
 * The clipping region is a rectangle, and starts off as the whole canvas every
 * frame. A matching number of "pop clip" commands must be issued as well.
 */
typedef struct {
    RenderCommand header;
    RenderClipRect bounds;
} RenderCommandPushClip;

/**
 * Undo the last "push clip".
 * See RenderCommandPushClip for more details on clipping.
 */
typedef struct {
    RenderCommand header;
} RenderCommandPopClip;

#define RENDER_PUSH_COMMAND(CmdType, CMD_ENUM, dl, ...)                        \
    do {                                                                       \
        CmdType *cmd =                                                         \
            link_buffer_alloc((dl).arena, sizeof(CmdType), alignof(CmdType));  \
        if ((dl).last)                                                         \
            (dl).last->next = &cmd->header;                                    \
        if (!(dl).first)                                                       \
            (dl).first = &cmd->header;                                         \
        *cmd = (CmdType){                                                      \
            .header =                                                          \
                {                                                              \
                    .type = CMD_ENUM,                                          \
                    .next = NULL,                                              \
                },                                                             \
            __VA_ARGS__                                                        \
        };                                                                     \
        (dl).last = &cmd->header;                                              \
    } while (0)

/** Push a clear to the display list. */
#define RENDER_CLEAR(dl, clear_color)                                          \
    RENDER_PUSH_COMMAND(                                                       \
        RenderCommandClear, RENDER_COMMAND_CLEAR, (dl), .color = (clear_color) \
    )

/**
 * Push a rectangle to the display list. Only the bounds and color have to be
 * specified. See RenderCommandRect for details.
 */
#define RENDER_RECT(dl, ...)                                                   \
    RENDER_PUSH_COMMAND(                                                       \
        RenderCommandRect, RENDER_COMMAND_RECT, (dl), __VA_ARGS__              \
    )

/** Push a line of text to the display list. Every field must be specified. */
#define RENDER_TEXT(dl, ...)                                                   \
    RENDER_PUSH_COMMAND(                                                       \
        RenderCommandText, RENDER_COMMAND_TEXT, (dl), __VA_ARGS__              \
    )

/** Push a clip rectangle to the display list. */
#define RENDER_PUSH_CLIP(dl, clip_rect)                                        \
    RENDER_PUSH_COMMAND(                                                       \
        RenderCommandPushClip,                                                 \
        RENDER_COMMAND_PUSH_CLIP,                                              \
        (dl),                                                                  \
        .bounds = (clip_rect)                                                  \
    )

/** Add popping the last clip rectangle to the display list. */
#define RENDER_POP_CLIP(dl)                                                    \
    RENDER_PUSH_COMMAND(RenderCommandPopClip, RENDER_COMMAND_POP_CLIP, (dl))
