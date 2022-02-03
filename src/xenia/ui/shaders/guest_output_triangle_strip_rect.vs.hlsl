cbuffer XeTriangleStripRectConstants : register(b0) {
  float2 xe_triangle_strip_rect_offset;
  // Can be negative.
  float2 xe_triangle_strip_rect_size;
};

void main(uint xe_vertex_id : SV_VertexID,
          out float4 xe_position : SV_Position) {
  // Passthrough - coordinate system differences are to be handled externally.
  xe_position = float4(xe_triangle_strip_rect_offset +
                           float2((xe_vertex_id >> uint2(0u, 1u)) & 1u) *
                           xe_triangle_strip_rect_size,
                       0.0f, 1.0f);
}
