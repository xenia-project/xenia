// NOTE: This file is compiled and embedded into the exe.
//       Use `xenia-build genspirv` and check in any changes under bin/.

#version 450 core
#extension all : warn
#extension GL_ARB_shading_language_420pack : require
#extension GL_ARB_separate_shader_objects : require
#extension GL_ARB_explicit_attrib_location : require

layout(push_constant) uniform push_consts_type {
  vec4 window_scale;
  vec4 vtx_fmt;
  vec4 point_size;
  vec4 alpha_test;
  uint ps_param_gen;
} push_constants;

in gl_PerVertex {
  vec4 gl_Position;
  // float gl_ClipDistance[];
} gl_in[];

out gl_PerVertex {
  vec4 gl_Position;
  // float gl_ClipDistance[];
};

layout(location = 0) in vec4 in_interpolators[][16];
layout(location = 16) in vec2 in_point_coord_unused[];
layout(location = 17) in float point_size[];

layout(location = 0) out vec4 out_interpolators[16];
layout(location = 16) out vec2 point_coord;

// TODO(benvanik): clamp to min/max.
// TODO(benvanik): figure out how to see which interpolator gets adjusted.

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

void main() {
  const vec2 offsets[4] = {
    vec2(-1.0,  1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
  };
  vec4 pos = gl_in[0].gl_Position;
  vec2 window_scaled_psize = push_constants.point_size.xy;
  // Shader header writes -1.0f to pointSize by default, so any positive value
  // means that it was overwritten by the translated vertex shader.
  if (point_size[0] > 0.0f) {
    window_scaled_psize = vec2(point_size[0]);
  }
  window_scaled_psize /= push_constants.window_scale.zw;
  for (int i = 0; i < 4; ++i) {
    gl_Position = vec4(pos.xy + (offsets[i] * window_scaled_psize), pos.zw);
    out_interpolators = in_interpolators[0];
    point_coord = max(offsets[i], vec2(0.0f));
    EmitVertex();
  }
  EndPrimitive();
}
