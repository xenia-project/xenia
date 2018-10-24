// NOTE: This file is compiled and embedded into the exe.
//       Use `xenia-build genspirv` and check in any changes under bin/.

#version 450 core
#extension all : warn
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
layout(line_strip, max_vertices = 5) out;
void main() {
  gl_Position = gl_in[0].gl_Position;
  gl_PointSize = gl_in[0].gl_PointSize;
  out_interpolators = in_interpolators[0];
  EmitVertex();
  gl_Position = gl_in[1].gl_Position;
  gl_PointSize = gl_in[1].gl_PointSize;
  out_interpolators = in_interpolators[1];
  EmitVertex();
  gl_Position = gl_in[2].gl_Position;
  gl_PointSize = gl_in[2].gl_PointSize;
  out_interpolators = in_interpolators[2];
  EmitVertex();
  gl_Position = gl_in[3].gl_Position;
  gl_PointSize = gl_in[3].gl_PointSize;
  out_interpolators = in_interpolators[3];
  EmitVertex();
  gl_Position = gl_in[0].gl_Position;
  gl_PointSize = gl_in[0].gl_PointSize;
  out_interpolators = in_interpolators[0];
  EmitVertex();
  EndPrimitive();
}
