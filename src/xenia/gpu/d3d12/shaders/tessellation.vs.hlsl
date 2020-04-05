#include "byte_swap.hlsli"
#include "xenos_draw.hlsli"

void main(uint xe_vertex_id_or_edge_factor : SV_VertexID,
          out XeHSControlPointInput xe_hs_input_out,
          out float4 xe_position_out : SV_Position) {
  xe_hs_input_out.index_or_edge_factor =
      asint(XeByteSwap(xe_vertex_id_or_edge_factor, xe_vertex_index_endian)) +
      xe_vertex_base_index;
  xe_position_out = (0.0f).xxxx;
}
