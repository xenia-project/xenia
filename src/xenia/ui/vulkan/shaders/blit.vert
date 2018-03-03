// NOTE: This file is compiled and embedded into the exe.
//       Use `xenia-build genspirv` and check in any changes under bin/.

#version 450 core
precision highp float;

layout(push_constant) uniform PushConstants {
  // normalized [x, y, w, h]
  layout(offset = 0x00) vec4 src_uv;
  layout(offset = 0x10) vec4 dst_uv;
} push_constants;

layout(location = 0) out vec2 vtx_uv;

void main() {
  const vec2 vtx_arr[4]=vec2[4](
    vec2(0,0),
    vec2(1,0),
    vec2(0,1),
    vec2(1,1)
  );
  
  vec2 vfetch_pos = vtx_arr[gl_VertexIndex];
  vec2 scaled_pos = vfetch_pos.xy * vec2(2.0, 2.0) - vec2(1.0, 1.0);
  vec4 scaled_dst_uv = push_constants.dst_uv * vec4(2.0);
  gl_Position =
      vec4(scaled_dst_uv.xy - vec2(1.0) + vfetch_pos.xy * scaled_dst_uv.zw, 0.0,
           1.0);

  vtx_uv = vfetch_pos.xy * push_constants.src_uv.zw + push_constants.src_uv.xy;
}