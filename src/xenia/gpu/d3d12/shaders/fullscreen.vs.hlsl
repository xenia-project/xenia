// A triangle covering the whole viewport.
float4 main(uint xe_vertex_id : SV_VertexID) : SV_Position {
  return float4(((xe_vertex_id.xx >> uint2(0u, 1u)) & 1u) * 4.0 - 1.0, 0.0,
                1.0);
}
