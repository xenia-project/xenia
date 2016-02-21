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
layout(triangle_strip, max_vertices = 4) out;
void main() {
  const int order[4] = { 0, 1, 3, 2 };
  for (int i = 0; i < 4; ++i) {
    int input_index = order[i];
    gl_Position = gl_in[input_index].gl_Position;
    gl_PointSize = gl_in[input_index].gl_PointSize;
    out_vtx = in_vtx[input_index];
    EmitVertex();
  }
  EndPrimitive();
}
