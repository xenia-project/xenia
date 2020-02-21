float4 main(uint xe_vertex_id : SV_VertexID) : SV_Position {
  uint vertex_id = xe_vertex_id % 3u;
  return float4(float2((vertex_id >> uint2(0u, 1u)) & 1u), 0.0, 1.0);
}
