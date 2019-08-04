#include "xenos_draw.hlsli"

[maxvertexcount(4)]
void main(lineadj XeVertex xe_in[4], inout TriangleStream<XeVertex> xe_stream) {
  xe_stream.Append(xe_in[0]);
  xe_stream.Append(xe_in[1]);
  xe_stream.Append(xe_in[3]);
  xe_stream.Append(xe_in[2]);
  xe_stream.RestartStrip();
}
