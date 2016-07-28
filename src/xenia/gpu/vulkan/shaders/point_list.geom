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

// TODO(benvanik): fetch default point size from register and use that if
//     the VS doesn't write oPointSize.
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
  float psize = gl_in[0].gl_PointSize;
  for (int i = 0; i < 4; ++i) {
    gl_Position = vec4(pos.xy + offsets[i] * psize, pos.zw);
    out_interpolators = in_interpolators[0];
    EmitVertex();
  }
  EndPrimitive();
}
