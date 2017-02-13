// NOTE: This file is compiled and embedded into the exe.
//       Use `xenia-build genspirv` and check in any changes under bin/.

#version 450 core
precision highp float;

layout(push_constant) uniform PushConstants {
  layout(offset = 0) mat4 projection_matrix;
} push_constants;

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;

layout(location = 0) out vec2 vtx_uv;
layout(location = 1) out vec4 vtx_color;

void main() {
  gl_Position = push_constants.projection_matrix * vec4(in_pos.xy, 0.0, 1.0);
  gl_Position.y = -gl_Position.y;
  vtx_uv = in_uv;
  vtx_color = in_color;
}
