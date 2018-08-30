#include "xenos_draw.hlsli"

[maxvertexcount(4)]
void main(point XeVertex xe_in[1], inout TriangleStream<XeVertex> xe_stream) {
  XeVertex xe_out;
  xe_out.interpolators = xe_in[0].interpolators;
  xe_out.position.zw = xe_in[0].position.zw;
  xe_out.point_params.z = xe_in[0].point_params.z;

  // Shader header writes -1.0f to point_size by default, so any positive value
  // means that it was overwritten by the translated vertex shader.
  float2 point_size =
      (xe_in[0].point_params.z > 0.0f ? xe_in[0].point_params.zz
                                      : xe_point_size) * xe_ndc_scale.xy;

  xe_out.point_params.xy = float2(0.0, 1.0);
  xe_out.position.xy = xe_in[0].position.xy + float2(-1.0, 1.0) * point_size;
  xe_stream.Append(xe_out);
  xe_out.point_params.xy = float2(1.0, 1.0);
  xe_out.position.xy = xe_in[0].position.xy + point_size;
  xe_stream.Append(xe_out);
  xe_out.point_params.xy = float2(0.0, 0.0);
  xe_out.position.xy = xe_in[0].position.xy - point_size;
  xe_stream.Append(xe_out);
  xe_out.point_params.xy = float2(1.0, 0.0);
  xe_out.position.xy = xe_in[0].position.xy + float2(1.0, -1.0) * point_size;
  xe_stream.Append(xe_out);
  xe_stream.RestartStrip();
}
