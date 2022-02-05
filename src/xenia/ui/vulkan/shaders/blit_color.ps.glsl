// NOTE: This file is compiled and embedded into the exe.
//       Use `xenia-build genspirv` and check in any changes under bin/.

#version 450 core
precision highp float;

layout(push_constant) uniform PushConstants {
  layout(offset = 0x20) vec3 _pad;
  layout(offset = 0x2C) int swap;
} push_constants;

layout(set = 0, binding = 0) uniform sampler2D src_texture;

layout(location = 0) in vec2 vtx_uv;
layout(location = 0) out vec4 oC;

void main() {
  oC = texture(src_texture, vtx_uv);
  if (push_constants.swap != 0) oC = oC.bgra;
}