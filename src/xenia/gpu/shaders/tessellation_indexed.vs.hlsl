#include "endian.xesli"
#include "xenos_draw.hlsli"

XeHSControlPointInputIndexed main(uint xe_vertex_id : SV_VertexID) {
  XeHSControlPointInputIndexed output;
  // Only the lower 24 bits of the vertex index are used (tested on an Adreno
  // 200 phone). `((index & 0xFFFFFF) + offset) & 0xFFFFFF` is the same as
  // `(index + offset) & 0xFFFFFF`.
  output.index =
      float(clamp((XeEndianSwap32(xe_vertex_id, xe_vertex_index_endian) +
                   xe_vertex_index_offset) &
                  0xFFFFFFu,
                  xe_vertex_index_min_max.x, xe_vertex_index_min_max.y));
  return output;
}
