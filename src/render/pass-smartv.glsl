#version 300 es

precision highp float;
uniform highp sampler2D tex;
uniform uint scale;
out float color;

float quantize(float col) {
    // constant is 0x6f / 0xff
    return 1.0 - step(0.4352941176, col);
}

void main() {
    int size = 8 * int(scale) / 120;
    int height = textureSize(tex, 0).y;
    float sum = 0.0;
    ivec2 center = ivec2(gl_FragCoord.xy);
    for (int y = center.y - size; y <= center.y + size; y++) {
        sum += texelFetch(tex, ivec2(center.x, clamp(y, 0, height - 1)), 0).r;
    }

    color = quantize(sum / (2.0 * float(size) + 1.0));
}
