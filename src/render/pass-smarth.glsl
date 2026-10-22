#version 300 es

precision highp float;
uniform highp sampler2D tex;
uniform uint scale;
out float color;

float grayscale(vec4 col) {
    // TODO: gamma correction?
    return 0.2126 * col.r + 0.7152 * col.g + 0.0722 * col.b;
}

void main() {
    int size = 8 * int(scale) / 120;
    float sum = 0.0;
    int width = textureSize(tex, 0).x;
    ivec2 center = ivec2(gl_FragCoord.xy);
    for (int x = center.x - size; x <= center.x + size; x++) {
        sum += grayscale(texelFetch(tex, ivec2(clamp(x, 0, width - 1), center.y), 0));
    }

    color = sum / (2.0 * float(size) + 1.0);
}
