// NOTE: This file is compiled and embedded into the exe.
//       Use `xenia-build genspirv` and check in any changes under bin/.

#version 450 core
#extension all : warn
#extension GL_ARB_shading_language_420pack : require
#extension GL_ARB_separate_shader_objects : require
#extension GL_ARB_explicit_attrib_location : require

layout(set = 0, binding = 1) uniform consts_type {
  vec4 float_consts[512];
  uint loop_consts[32];
  uint bool_consts[8];
} consts;

layout(push_constant) uniform push_consts_type {
  vec4 window_scale;
  vec4 vtx_fmt;
  vec4 point_size;
  vec4 alpha_test;
  uint ps_param_gen;
} push_constants;

layout(set = 1, binding = 0) uniform sampler1D textures1D[32];
layout(set = 1, binding = 1) uniform sampler2D textures2D[32];
layout(set = 1, binding = 2) uniform sampler3D textures3D[32];
layout(set = 1, binding = 3) uniform samplerCube textures4D[32];

layout(location = 0) in vec4 in_interpolators[16];
layout(location = 0) out vec4 oC[4];

void main() {
  // This shader does absolutely nothing!
  return;
}
