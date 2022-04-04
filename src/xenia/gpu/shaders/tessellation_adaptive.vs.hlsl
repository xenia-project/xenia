#include "endian.xesli"
#include "xenos_draw.hlsli"

XeHSControlPointInputAdaptive main(uint xe_edge_factor : SV_VertexID) {
  XeHSControlPointInputAdaptive output;
  // The Xbox 360's GPU accepts the float32 tessellation factors for edges
  // through a special kind of an index buffer.
  // While 4D5307F2 sets the factors to 0 for frustum-culled (quad) patches, in
  // 4D5307E6 only allowing patches with factors above 0 makes distant
  // (triangle) patches disappear - it appears that there are no special values
  // for culled patches on the Xbox 360 (unlike zero, negative and NaN on
  // Direct3D 11).
  output.edge_factor = clamp(
      asfloat(XeEndianSwap32(xe_edge_factor, xe_vertex_index_endian)) + 1.0f,
      xe_tessellation_factor_range.x, xe_tessellation_factor_range.y);
  return output;
}
