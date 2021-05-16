#include "endian.hlsli"
#include "xenos_draw.hlsli"

XeHSControlPointInputIndexed main(uint xe_vertex_id : SV_VertexID) {
  XeHSControlPointInputIndexed output;
  output.index =
      float(asint(XeEndianSwap32(xe_vertex_id, xe_vertex_index_endian)) +
            xe_vertex_base_index);
  return output;
}
