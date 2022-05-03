#include "xenos_draw.hlsli"

// TODO(Triang3l): Figure out how to see which interpolator gets adjusted.

[maxvertexcount(4)]
void main(point XeVertexPreGS xe_in[1],
          inout TriangleStream<XeVertexPostGS> xe_stream) {
  // TODO(Triang3l): Handle ps_ucp_mode.
  if (xe_in[0].cull_distance < 0.0 || any(isnan(xe_in[0].post_gs.position))) {
    return;
  }

  // The vertex shader's header writes -1.0f to point_size by default, so any
  // non-negative value means that it was overwritten by the translated vertex
  // shader.
  float2 point_radius = xe_point_constant_radius;
  float point_vertex_diameter = xe_in[0].post_gs.pre_ps.point_params.z;
  if (point_vertex_diameter >= 0.0) {
    // Already clamped in the vertex shader (combined with making it
    // non-negative).
    point_radius = point_vertex_diameter * 0.5;
  }
  if (!all(point_radius > 0.0)) {
    // 4D5307F1 has zero-size snowflakes, drop them quicker.
    return;
  }
  point_radius *= xe_point_screen_to_ndc;
  point_radius *= xe_in[0].post_gs.position.w;

  XeVertexPostGS xe_out;
  xe_out.pre_ps.interpolators = xe_in[0].post_gs.pre_ps.interpolators;
  xe_out.pre_ps.point_params.z = xe_in[0].post_gs.pre_ps.point_params.z;
  xe_out.position.zw = xe_in[0].post_gs.position.zw;
  // TODO(Triang3l): Handle ps_ucp_mode.
  xe_out.clip_distance_0123 = xe_in[0].post_gs.clip_distance_0123;
  xe_out.clip_distance_45 = xe_in[0].post_gs.clip_distance_45;

  // V = 0 in the top (+Y in Direct3D), 1 in the bottom, according to the
  // analysis of Adreno 200 behavior (V = 1 towards -gl_FragCoord.y, the bottom,
  // but the top-left rule is used for rasterization, and gl_FragCoord is
  // generated from |PsParamGen.xy| via multiply-addition as opposed to just
  // addition, so -gl_FragCoord.y is likely positive in screen coordinates, or
  // +|PsParamGen.y|).
  // TODO(Triang3l): On Vulkan, sign of Y needs to inverted because of the
  // upper-left origin.
  xe_out.pre_ps.point_params.xy = float2(0.0, 0.0);
  xe_out.position.xy =
      xe_in[0].post_gs.position.xy + float2(-point_radius.x, point_radius.y);
  xe_stream.Append(xe_out);
  xe_out.pre_ps.point_params.xy = float2(0.0, 1.0);
  xe_out.position.xy = xe_in[0].post_gs.position.xy - point_radius;
  xe_stream.Append(xe_out);
  xe_out.pre_ps.point_params.xy = float2(1.0, 0.0);
  xe_out.position.xy = xe_in[0].post_gs.position.xy + point_radius;
  xe_stream.Append(xe_out);
  xe_out.pre_ps.point_params.xy = float2(1.0, 1.0);
  xe_out.position.xy =
      xe_in[0].post_gs.position.xy + float2(point_radius.x, -point_radius.y);
  xe_stream.Append(xe_out);
  xe_stream.RestartStrip();
}
