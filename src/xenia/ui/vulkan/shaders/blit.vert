// NOTE: This file is compiled and embedded into the exe.
//       Use `xenia-build genspirv` and check in any changes under bin/.

#version 450 core
precision highp float;

layout(push_constant) uniform PushConstants {
  // normalized [x, y, w, h]
  layout(offset = 0) vec4 src_uv;
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
  gl_Position = vec4(vfetch_pos.xy * vec2(2.0, 2.0) -
                vec2(1.0, 1.0), 0.0, 1.0);
  vtx_uv = vfetch_pos.xy * push_constants.src_uv.zw +
                           push_constants.src_uv.xy;
}