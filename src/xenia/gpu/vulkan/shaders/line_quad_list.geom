// NOTE: This file is compiled and embedded into the exe.
//       Use `xenia-build genspirv` and check in any changes under bin/.

#version 450 core
#extension all : warn

in gl_PerVertex {
  vec4 gl_Position;
  float gl_PointSize;
  float gl_ClipDistance[];
} gl_in[];

out gl_PerVertex {
  vec4 gl_Position;
  float gl_PointSize;
  float gl_ClipDistance[];
};

struct VertexData {
  vec4 o[16];
};
layout(location = 0) in VertexData in_vtx[];
layout(location = 0) out VertexData out_vtx;

layout(lines_adjacency) in;
layout(line_strip, max_vertices = 5) out;
void main() {
  gl_Position = gl_in[0].gl_Position;
  gl_PointSize = gl_in[0].gl_PointSize;
  out_vtx = in_vtx[0];
  EmitVertex();
  gl_Position = gl_in[1].gl_Position;
  gl_PointSize = gl_in[1].gl_PointSize;
  out_vtx = in_vtx[1];
  EmitVertex();
  gl_Position = gl_in[2].gl_Position;
  gl_PointSize = gl_in[2].gl_PointSize;
  out_vtx = in_vtx[2];
  EmitVertex();
  gl_Position = gl_in[3].gl_Position;
  gl_PointSize = gl_in[3].gl_PointSize;
  out_vtx = in_vtx[3];
  EmitVertex();
  gl_Position = gl_in[0].gl_Position;
  gl_PointSize = gl_in[0].gl_PointSize;
  out_vtx = in_vtx[0];
  EmitVertex();
  EndPrimitive();
}
