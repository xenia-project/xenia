#include "xenos_draw.hlsli"

[maxvertexcount(4)]
void main(lineadj XeVertexPreGS xe_in[4],
          inout TriangleStream<XeVertexPostGS> xe_stream) {
  // Culling should probably be done per-triangle - while there's no
  // RETAIN_QUADS on Adreno 2xx, on R6xx it's always disabled for
  // non-tessellated quads, so they are always decomposed into triangles.
  // Therefore, not doing any cull distance or NaN position checks here.
  // TODO(Triang3l): Find whether vertex killing should actually work for each
  // triangle or for the entire quad.
  // TODO(Triang3l): Find the correct order.
  XeVertexPostGS xe_out;
  xe_out = xe_in[0].post_gs;
  xe_stream.Append(xe_out);
  xe_out = xe_in[1].post_gs;
  xe_stream.Append(xe_out);
  xe_out = xe_in[3].post_gs;
  xe_stream.Append(xe_out);
  xe_out = xe_in[2].post_gs;
  xe_stream.Append(xe_out);
  xe_stream.RestartStrip();
}
