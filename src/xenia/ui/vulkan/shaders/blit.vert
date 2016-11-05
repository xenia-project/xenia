// NOTE: This file is compiled and embedded into the exe.
//       Use `xenia-build genspirv` and check in any changes under bin/.

#version 450 core
precision highp float;

layout(push_constant) uniform PushConstants {
  vec4 src_uv;
} push_constants;

layout(location = 0) in vec2 vfetch_pos;
layout(location = 0) out vec2 vtx_uv;

void main() {
  gl_Position = vec4(vfetch_pos.xy * vec2(2.0, -2.0) -
                vec2(1.0, -1.0), 0.0, 1.0);
  vtx_uv = vfetch_pos.xy * push_constants.src_uv.zw +
                           push_constants.src_uv.xy;
}