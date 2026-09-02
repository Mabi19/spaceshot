#version 300 es

uniform vec2 screen_size;
layout(location = 0) in vec2 v_pos;
layout(location = 1) in vec2 pos;
layout(location = 2) in vec2 size;
layout(location = 3) in vec4 color;
layout(location = 4) in vec4 border_radius;
layout(location = 5) in vec4 border_width;
layout(location = 6) in vec4 border_color;
layout(location = 7) in vec2 uv_tl;
layout(location = 8) in vec2 uv_br;

out vec4 rect_color;
out vec4 rect_border_radius;
out vec4 rect_border_width;
out vec4 rect_border_color;
out vec2 uv;
// Fragment position in rect-local pixels; (0, 0) is the rect's top-left
// corner. May lie slightly outside [0, size] because of the fringe (below).
out vec2 local_pos;
out vec2 rect_size;

void main() {
    // We specify our rects in a coordinate system where (0, 0) is the top left corner,
    // and (screen_w, screen_h) is the bottom right corner of the screen.
    // GL expects (0, 0) to be the middle, +x to go right, +y to go *up*,
    // and the edges of the screen are at -1 and 1.

    // The quad is expanded by a 1px fringe on every side, so that the
    // antialiased edge computed in the fragment shader (which extends
    // slightly outside the rect) is not clipped away.
    vec2 screen_pos = v_pos * (size + 2.0) + pos - 1.0;
    vec2 ndc_pos = vec2(screen_pos / (screen_size * 0.5) - 1.0);
    ndc_pos.y = -ndc_pos.y;
    gl_Position = vec4(ndc_pos, 0.0, 1.0);

    // Also pass the necessary data to the fragment shader.
    rect_color = color;
    rect_border_radius = border_radius;
    rect_border_width = border_width;
    rect_border_color = border_color;
    rect_size = size;
    local_pos = v_pos * (size + 2.0) - 1.0;
    // UVs span the *unexpanded* rect; the fringe samples slightly outside the
    // given range, which textures must handle via GL_CLAMP_TO_EDGE.
    uv = uv_tl + (local_pos / size) * (uv_br - uv_tl);
}
