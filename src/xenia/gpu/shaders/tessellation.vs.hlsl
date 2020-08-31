#include "endian.hlsli"
#include "xenos_draw.hlsli"

XeHSControlPointInput main(uint xe_vertex_id_or_edge_factor : SV_VertexID) {
  XeHSControlPointInput output;
  output.index_or_edge_factor =
      asint(XeEndianSwap32(xe_vertex_id_or_edge_factor,
                           xe_vertex_index_endian)) +
      xe_vertex_base_index;
  return output;
}
