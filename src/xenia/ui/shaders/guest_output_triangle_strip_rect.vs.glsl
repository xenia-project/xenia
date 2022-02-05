#version 310 es

layout(push_constant) uniform XeTriangleStripRectConstants {
  // If the layout is changed, update the base offset in all guest output
  // fragment shaders!
  vec2 xe_triangle_strip_rect_offset;
  // Can be negative.
  vec2 xe_triangle_strip_rect_size;
};

void main() {
  // Passthrough - coordinate system differences are to be handled externally.
  gl_Position = vec4(xe_triangle_strip_rect_offset +
                         vec2((uvec2(gl_VertexIndex) >> uvec2(0u, 1u)) & 1u) *
                         xe_triangle_strip_rect_size,
                     0.0, 1.0);
}
