#version 310 es
precision highp float;

layout(location = 0) in vec2 xe_var_texcoord;

layout(location = 0) out lowp vec4 xe_frag_color;

void main() {
  xe_frag_color = vec4(xe_var_texcoord, 0.0, 1.0);
}
