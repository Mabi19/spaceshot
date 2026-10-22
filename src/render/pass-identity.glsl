#version 300 es

precision highp float;
uniform highp sampler2D tex;
out vec4 color;

void main() {
    color = texelFetch(tex, ivec2(gl_FragCoord.xy), 0);
}
