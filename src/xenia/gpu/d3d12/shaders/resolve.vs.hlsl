// A triangle covering the whole viewport.
float4 main(uint xe_vertex_id : SV_VertexID) : SV_Position {
  return float4(float2(uint2(xe_vertex_id, xe_vertex_id << 1u) & 2u) *
                float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
