// NOTE: This file is compiled and embedded into the exe.
//       Use `xenia-build genspirv` and check in any changes under bin/.

#version 450 core
#extension all : warn
#extension GL_ARB_shading_language_420pack : require
#extension GL_ARB_separate_shader_objects : require
#extension GL_ARB_explicit_attrib_location : require

in gl_PerVertex {
  vec4 gl_Position;
  float gl_PointSize;
  // float gl_ClipDistance[];
} gl_in[];

out gl_PerVertex {
  vec4 gl_Position;
  float gl_PointSize;
  // float gl_ClipDistance[];
};

layout(location = 0) in vec4 in_interpolators[][16];
layout(location = 0) out vec4 out_interpolators[16];

layout(location = 16) in vec2 _in_point_coord_unused[];
layout(location = 17) in float _in_point_size_unused[];

layout(location = 16) out vec2 _out_point_coord_unused;

layout(lines_adjacency) in;
layout(triangle_strip, max_vertices = 4) out;
void main() {
  const int order[4] = { 0, 1, 3, 2 };
  for (int i = 0; i < 4; ++i) {
    int input_index = order[i];
    gl_Position = gl_in[input_index].gl_Position;
    gl_PointSize = gl_in[input_index].gl_PointSize;
    out_interpolators = in_interpolators[input_index];
    EmitVertex();
  }
  EndPrimitive();
}
