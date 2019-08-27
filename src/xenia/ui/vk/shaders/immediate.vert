#version 450 core
precision highp float;

layout(location = 0) in vec2 xe_in_position;
layout(location = 1) in vec2 xe_in_texcoord;
layout(location = 2) in vec4 xe_in_color;

layout(location = 0) out vec2 xe_out_texcoord;
layout(location = 1) out vec4 xe_out_color;

layout(push_constant) uniform XePushConstants {
  layout(offset = 0) vec2 viewport_inv_size;
} xe_push_constants;

void main() {
  gl_Position = vec4(
      xe_in_position * xe_push_constants.viewport_inv_size * 2.0 - 1.0, 0.0,
      1.0);
  xe_out_texcoord = xe_in_texcoord;
  xe_out_color = xe_in_color;
}
