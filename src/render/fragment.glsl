#version 300 es

// The calculations below happen in device pixels, for which mediump's
// 10-bit mantissa is not precise enough on bigger screens.
precision highp float;

uniform sampler2D tex;
in vec4 rect_color;
// x = tl, y = tr, z = bl, w = br
in vec4 rect_border_radius;
// x = left, y = right, z = top, w = bottom
in vec4 rect_border_width;
in vec4 rect_border_color;
in vec2 uv;
// Fragment position in rect-local pixels; (0, 0) is the rect's top-left corner.
in vec2 local_pos;
in vec2 rect_size;

out vec4 color;

/**
 * Signed distance to a rounded box in pixels: negative inside, positive
 * outside, zero on the edge. p is relative to the box's center (note that y
 * points *down*), b is the box's half-size, and r contains the per-corner
 * radii in the order (tl, tr, bl, br).
 */
float sd_rounded_box(vec2 p, vec2 b, vec4 r) {
    // Select the radius of the corner closest to p.
    vec2 row = p.y > 0.0 ? r.zw : r.xy;
    float radius = p.x > 0.0 ? row.y : row.x;
    vec2 q = abs(p) - b + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - radius;
}

/**
 * Convert a signed distance into an antialiased coverage value.
 * The ramp is exactly one pixel wide and centered on the edge.
 *
 * Typically, this would use fwidth(d) to know how much it changes over a pixel,
 * so that the AA smoothing is always 1px wide regardless of scale.
 * But we always render at a 1:1 scale, with no weird transformations.
 */
float sdf_coverage(float d) {
    return 1.0 - smoothstep(-0.5, 0.5, d);
}

void main() {
    vec2 half_size = rect_size * 0.5;
    vec2 p = local_pos - half_size;

    float outer_coverage =
        sdf_coverage(sd_rounded_box(p, half_size, rect_border_radius));

    // The border is inset, so its inner edge is the rect shrunk by the
    // per-side border widths.
    vec4 bw = rect_border_width;
    vec2 inner_tl = bw.xz;
    vec2 inner_br = rect_size - bw.yw;
    vec2 inner_half_size = (inner_br - inner_tl) * 0.5;
    vec2 inner_center = (inner_tl + inner_br) * 0.5;
    // To look good, inner radius + border width = outer radius. Every corner
    // touches two (potentially different) border widths;
    // reduce by the thicker one.
    vec4 inner_radii = max(
            rect_border_radius - vec4(
                    max(bw.x, bw.z), // tl: left, top
                    max(bw.y, bw.z), // tr: right, top
                    max(bw.x, bw.w), // bl: left, bottom
                    max(bw.y, bw.w) // br: right, bottom
                ),
            vec4(0.0)
        );

    float inner_coverage = sdf_coverage(
            sd_rounded_box(local_pos - inner_center, inner_half_size, inner_radii)
        );

    // The border covers the band between the outer and inner edges.
    float border_coverage = clamp(outer_coverage - inner_coverage, 0.0, 1.0);

    // All colors are premultiplied, so coverage can simply be multiplied in,
    // and "over" compositing is src + dst * (1 - src.a).
    vec4 fill = rect_color * texture(tex, uv) * outer_coverage;
    vec4 border = rect_border_color * border_coverage;
    color = border + fill * (1.0 - border.a);
}
