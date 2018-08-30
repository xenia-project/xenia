#include "xenos_draw.hlsli"

[maxvertexcount(6)]
void main(triangle XeVertex xe_in[3],
          inout TriangleStream<XeVertex> xe_stream) {
  XeVertex xe_out;

  xe_stream.Append(xe_in[0]);
  xe_stream.Append(xe_in[1]);
  xe_stream.Append(xe_in[2]);
  xe_stream.RestartStrip();

  // Most games use a left-aligned form.
  [branch] if (all(xe_in[0].position.xy ==
                   float2(xe_in[2].position.x, xe_in[1].position.y)) ||
               all(xe_in[0].position.xy ==
                   float2(xe_in[1].position.x, xe_in[2].position.y))) {
    //  0 ------ 1   0:  -1,-1
    //  |      - |   1:   1,-1
    //  |   //   |   2:  -1, 1
    //  | -      |   3: [ 1, 1 ]
    //  2 ----- [3]
    //
    //  0 ------ 2   0:  -1,-1
    //  |      - |   1:  -1, 1
    //  |   //   |   2:   1,-1
    //  | -      |   3: [ 1, 1 ]
    //  1 ------[3]
    xe_stream.Append(xe_in[2]);
    xe_stream.Append(xe_in[1]);
    [unroll] for (int i = 0; i < 16; ++i) {
      xe_out.interpolators[i] = xe_in[1].interpolators[i] -
                                xe_in[0].interpolators[i] +
                                xe_in[2].interpolators[i];
    }
    xe_out.point_params.xy = xe_in[1].point_params.xy +
                             xe_in[0].point_params.xy -
                             xe_in[2].point_params.xy;
    xe_out.position = float4(xe_in[1].position.xy -
                             xe_in[0].position.xy +
                             xe_in[2].position.xy,
                             xe_in[2].position.zw);
  } else {
    //  0 ------ 1   0:  -1,-1
    //  | -      |   1:   1,-1
    //  |   \\   |   2:   1, 1
    //  |      - |   3: [-1, 1 ]
    // [3] ----- 2
    xe_stream.Append(xe_in[0]);
    xe_stream.Append(xe_in[2]);
    [unroll] for (int i = 0; i < 16; ++i) {
      xe_out.interpolators[i] = xe_in[0].interpolators[i] -
                                xe_in[1].interpolators[i] +
                                xe_in[2].interpolators[i];
    }
    xe_out.point_params.xy = xe_in[0].point_params.xy +
                             xe_in[1].point_params.xy -
                             xe_in[2].point_params.xy;
    xe_out.position = float4(xe_in[0].position.xy -
                             xe_in[1].position.xy +
                             xe_in[2].position.xy,
                             xe_in[2].position.zw);
  }
  xe_out.point_params.z = xe_in[2].point_params.z;
  xe_stream.Append(xe_out);
  xe_stream.RestartStrip();
}
