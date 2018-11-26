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

layout(triangles) in;
layout(triangle_strip, max_vertices = 6) out;

bool equalsEpsilon(vec2 left, vec2 right, float epsilon) {
  return all(lessThanEqual(abs(left - right), vec2(epsilon)));
}

void main() {
  // Most games use a left-aligned form.
  if (equalsEpsilon(gl_in[0].gl_Position.xy, vec2(gl_in[2].gl_Position.x, gl_in[1].gl_Position.y), 0.001) ||
      equalsEpsilon(gl_in[0].gl_Position.xy, vec2(gl_in[1].gl_Position.x, gl_in[2].gl_Position.y), 0.001)) {
    //  0 ------ 1   0:  -1,-1
    //  |      - |   1:   1,-1
    //  |   //   |   2:  -1, 1
    //  | -      |   3: [ 1, 1 ]
    //  2 ----- [3]
    //
    //  0 ------ 2   0:  -1,-1
    //  |      - |   1:  -1, 1
    //  |   //   |   2:   1,-1
    //  | -      |   3: [ 1, 1 ]
    //  1 ------[3]
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
    gl_Position = vec4((-gl_in[0].gl_Position.xy) +
                         gl_in[1].gl_Position.xy +
                         gl_in[2].gl_Position.xy,
                         gl_in[2].gl_Position.zw);
    gl_PointSize = gl_in[2].gl_PointSize;
    for (int i = 0; i < 16; ++i) {
      out_interpolators[i] = (-in_interpolators[0][i]) +
                               in_interpolators[1][i] +
                               in_interpolators[2][i];
    }
    EmitVertex();
    EndPrimitive();
  } else {
    //  0 ------ 1   0:  -1,-1
    //  | -      |   1:   1,-1
    //  |   \\   |   2:   1, 1
    //  |      - |   3: [-1, 1 ]
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
    gl_Position = vec4(  gl_in[0].gl_Position.xy +
                       (-gl_in[1].gl_Position.xy) +
                         gl_in[2].gl_Position.xy,
                         gl_in[2].gl_Position.zw);
    gl_PointSize = gl_in[2].gl_PointSize;
    for (int i = 0; i < 16; ++i) {
      out_interpolators[i] =   in_interpolators[0][i] +
                             (-in_interpolators[1][i]) +
                               in_interpolators[2][i];
    }
    EmitVertex();
    EndPrimitive();
  }
}
