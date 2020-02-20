#include "xenos_draw.hlsli"

[maxvertexcount(4)]
void main(lineadj XeVertexPreGS xe_in[4],
          inout TriangleStream<XeVertexPostGS> xe_stream) {
  // Must kill the whole quad if need to kill.
  if (max(max(xe_in[0].cull_distance, xe_in[1].cull_distance),
          max(xe_in[2].cull_distance, xe_in[3].cull_distance)) < 0.0f ||
      any(isnan(xe_in[0].post_gs.position)) ||
      any(isnan(xe_in[1].post_gs.position)) ||
      any(isnan(xe_in[2].post_gs.position)) ||
      any(isnan(xe_in[3].post_gs.position))) {
    return;
  }
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
