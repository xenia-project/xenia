#ifndef XENIA_GPU_D3D12_SHADERS_XENOS_DRAW_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_XENOS_DRAW_HLSLI_

cbuffer xe_system_cbuffer : register(b0) {
  uint xe_flags;
  float2 xe_tessellation_factor_range;
  uint xe_line_loop_closing_index;

  uint xe_vertex_index_endian;
  uint xe_vertex_index_offset;
  uint2 xe_vertex_index_min_max;

  float4 xe_user_clip_planes[6];

  float3 xe_ndc_scale;
  float xe_point_vertex_diameter_min;

  float3 xe_ndc_offset;
  float xe_point_vertex_diameter_max;

  float2 xe_point_constant_diameter;
  float2 xe_point_screen_diameter_to_ndc_radius;

  uint4 xe_texture_swizzled_signs[2];

  uint xe_textures_resolution_scaled;
  uint2 xe_sample_count_log2;
  float xe_alpha_test_reference;

  uint xe_alpha_to_mask;
  uint xe_edram_32bpp_tile_pitch_dwords_scaled;
  uint xe_edram_depth_base_dwords_scaled;

  float4 xe_color_exp_bias;

  float2 xe_edram_poly_offset_front;
  float2 xe_edram_poly_offset_back;

  uint4 xe_edram_stencil[2];

  uint4 xe_edram_rt_base_dwords_scaled;

  uint4 xe_edram_rt_format_flags;

  float4 xe_edram_rt_clamp[4];

  uint4 xe_edram_rt_keep_mask[2];

  uint4 xe_edram_rt_blend_factors_ops;

  float4 xe_edram_blend_constant;
};

struct XeHSControlPointInputIndexed {
  float index : XEVERTEXID;
};

struct XeHSControlPointInputAdaptive {
  // 1.0 added in the vertex shader to convert to Direct3D 11+, and clamped to
  // the factor range in the vertex shader.
  float edge_factor : XETESSFACTOR;
};

struct XeHSControlPointOutput {
  float index : XEVERTEXID;
};

struct XeVertexPrePS {
  float4 interpolators[16] : TEXCOORD0;
  float3 point_parameters : TEXCOORD16;
};

struct XeVertexPostGS {
  XeVertexPrePS pre_ps;
  // Precise needed to preserve NaN - guest primitives may be converted to more
  // than 1 triangle, so need to kill them entirely manually in GS if any vertex
  // is NaN.
  precise float4 position : SV_Position;
  float4 clip_distance_0123 : SV_ClipDistance0;
  float2 clip_distance_45 : SV_ClipDistance1;
};

struct XeVertexPreGS {
  XeVertexPostGS post_gs;
  // Guest primitives may be converted to more than 1 triangle, so need to kill
  // them entirely manually in GS - must kill if all guest primitive vertices
  // have negative cull distance.
  float cull_distance : SV_CullDistance;
};

#endif  // XENIA_GPU_D3D12_SHADERS_XENOS_DRAW_HLSLI_
