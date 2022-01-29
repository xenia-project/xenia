#version 310 es
precision highp float;

layout(set = 0, binding = 0) uniform lowp sampler2D xe_immediate_texture;

layout(location = 0) in vec2 xe_var_texcoord;
layout(location = 1) in lowp vec4 xe_var_color;

layout(location = 0) out lowp vec4 xe_frag_color;

void main() {
  xe_frag_color =
      xe_var_color * textureLod(xe_immediate_texture, xe_var_texcoord, 0.0);
}
