// A triangle covering the whole viewport.
void main(uint xe_vertex_id : SV_VertexID, out float2 xe_texcoord : TEXCOORD,
          out float4 xe_position : SV_Position) {
  xe_texcoord = float2(uint2(xe_vertex_id, xe_vertex_id << 1u) & 2u);
  xe_position =
      float4(xe_texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
