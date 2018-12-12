float4 main(uint xe_vertex_id : SV_VertexID) : SV_Position {
  uint2 position = (xe_vertex_id >> uint2(1u, 0u)) & 1u;
  position.y ^= position.x;
  return float4(float2(position), 0.0, 1.0);
}
