#version 310 es

// A triangle covering the whole viewport.

layout(location = 0) out vec2 xe_var_texcoord;

void main() {
  xe_var_texcoord = vec2(uvec2(gl_VertexIndex, gl_VertexIndex << 1u) & 2u);
  gl_Position = vec4(xe_var_texcoord * 2.0 - 1.0, 0.0, 1.0);
}
