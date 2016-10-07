// NOTE: This file is compiled and embedded into the exe.
//       Use `xenia-build genspirv` and check in any changes under bin/.

#version 450 core
#extension all : warn

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

layout(triangles) in;
layout(triangle_strip, max_vertices = 6) out;
void main() {
  // Most games use the left-aligned form.
  bool left_aligned = gl_in[0].gl_Position.x == gl_in[2].gl_Position.x;
  if (left_aligned) {
    //  0 ------ 1
    //  |      - |
    //  |   //   |
    //  | -      |
    //  2 ----- [3]
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
    EndPrimitive();
    gl_Position = gl_in[2].gl_Position;
    gl_PointSize = gl_in[2].gl_PointSize;
    out_interpolators = in_interpolators[2];
    EmitVertex();
    gl_Position = gl_in[1].gl_Position;
    gl_PointSize = gl_in[1].gl_PointSize;
    out_interpolators = in_interpolators[1];
    EmitVertex();
    gl_Position = (gl_in[1].gl_Position + gl_in[2].gl_Position) -
                  gl_in[0].gl_Position;
    gl_PointSize = gl_in[2].gl_PointSize;
    for (int i = 0; i < 16; ++i) {
      out_interpolators[i] = -in_interpolators[0][i] + in_interpolators[1][i] + in_interpolators[2][i];
    }
    EmitVertex();
    EndPrimitive();
  } else {
    //  0 ------ 1
    //  | -      |
    //  |   \\   |
    //  |      - |
    // [3] ----- 2
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
    EndPrimitive();
    gl_Position = gl_in[0].gl_Position;
    gl_PointSize = gl_in[0].gl_PointSize;
    out_interpolators = in_interpolators[0];
    EmitVertex();
    gl_Position = gl_in[2].gl_Position;
    gl_PointSize = gl_in[2].gl_PointSize;
    out_interpolators = in_interpolators[2];
    EmitVertex();
    gl_Position = (gl_in[0].gl_Position + gl_in[2].gl_Position) -
                  gl_in[1].gl_Position;
    gl_PointSize = gl_in[2].gl_PointSize;
    for (int i = 0; i < 16; ++i) {
      out_interpolators[i] = in_interpolators[0][i] + -in_interpolators[1][i] + in_interpolators[2][i];
    }
    EmitVertex();
    EndPrimitive();
  }
}
