#include "xenos_draw.hlsli"

// TODO(Triang3l): Figure out how to see which interpolator gets adjusted.

[maxvertexcount(4)]
void main(point XeVertexPreGS xe_in[1],
          inout TriangleStream<XeVertexPostGS> xe_stream) {
  if (xe_in[0].cull_distance < 0.0f || any(isnan(xe_in[0].post_gs.position))) {
    return;
  }

  XeVertexPostGS xe_out;
  xe_out.pre_ps.interpolators = xe_in[0].post_gs.pre_ps.interpolators;
  xe_out.pre_ps.point_params.z = xe_in[0].post_gs.pre_ps.point_params.z;
  xe_out.pre_ps.clip_space_zw = xe_in[0].post_gs.pre_ps.clip_space_zw;
  xe_out.position.zw = xe_in[0].post_gs.position.zw;
  xe_out.clip_distance_0123 = xe_in[0].post_gs.clip_distance_0123;
  xe_out.clip_distance_45 = xe_in[0].post_gs.clip_distance_45;

  // Shader header writes -1.0f to point_size by default, so any positive value
  // means that it was overwritten by the translated vertex shader.
  float2 point_size =
      xe_in[0].post_gs.pre_ps.point_params.z > 0.0f
           ? xe_in[0].post_gs.pre_ps.point_params.zz
           : float2(xe_point_size_x, xe_point_size_y);
  point_size =
      clamp(point_size, xe_point_size_min_max.xx, xe_point_size_min_max.yy) *
      xe_point_screen_to_ndc * xe_in[0].post_gs.position.w;

  xe_out.pre_ps.point_params.xy = float2(0.0, 0.0);
  // TODO(Triang3l): On Vulkan, sign of Y needs to inverted because of
  // upper-left origin.
  // TODO(Triang3l): Investigate the true signs of point sprites.
  xe_out.position.xy =
      xe_in[0].post_gs.position.xy + float2(-point_size.x, point_size.y);
  xe_stream.Append(xe_out);
  xe_out.pre_ps.point_params.xy = float2(0.0, 1.0);
  xe_out.position.xy = xe_in[0].post_gs.position.xy - point_size;
  xe_stream.Append(xe_out);
  xe_out.pre_ps.point_params.xy = float2(1.0, 0.0);
  xe_out.position.xy = xe_in[0].post_gs.position.xy + point_size;
  xe_stream.Append(xe_out);
  xe_out.pre_ps.point_params.xy = float2(1.0, 1.0);
  xe_out.position.xy =
      xe_in[0].post_gs.position.xy + float2(point_size.x, -point_size.y);
  xe_stream.Append(xe_out);
  xe_stream.RestartStrip();
}
