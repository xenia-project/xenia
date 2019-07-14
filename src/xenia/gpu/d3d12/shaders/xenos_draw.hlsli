#ifndef XENIA_GPU_D3D12_SHADERS_XENOS_DRAW_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_XENOS_DRAW_HLSLI_

cbuffer xe_system_cbuffer : register(b0) {
  uint xe_flags;
  uint xe_line_loop_closing_index;
  uint xe_vertex_index_endian_and_edge_factors;
  int xe_vertex_base_index;

  float4 xe_user_clip_planes[6];

  float3 xe_ndc_scale;
  uint xe_pixel_pos_reg;

  float3 xe_ndc_offset;
  float xe_pixel_half_pixel_offset;

  float2 xe_point_size;
  float2 xe_point_size_min_max;

  float2 xe_point_screen_to_ndc;
  uint2 xe_sample_count_log2;

  float xe_alpha_test_reference;
  uint xe_edram_resolution_square_scale;
  uint xe_edram_pitch_tiles;
  uint xe_edram_depth_base_dwords;

  float4 xe_color_exp_bias;

  uint4 xe_color_output_map;

  float2 xe_tessellation_factor_range;
  float2 xe_edram_depth_range;

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

struct XeVertex {
  float4 interpolators[16] : TEXCOORD0;
  float3 point_params : TEXCOORD16;
  float2 clip_space_zw : TEXCOORD17;
  float4 position : SV_Position;
  float4 clip_distance_0123 : SV_ClipDistance0;
  float2 clip_distance_45 : SV_ClipDistance1;
};

#define XeSysFlag_SharedMemoryIsUAV_Shift 0u
#define XeSysFlag_SharedMemoryIsUAV (1u << XeSysFlag_SharedMemoryIsUAV_Shift)

uint XeGetTessellationFactorAddress(uint control_point_id,
                                    uint control_points_per_patch) {
  // TODO(Triang3l): Verify whether the index offset is applied correctly.
  control_point_id += asuint(xe_vertex_base_index) * control_points_per_patch;
  return (xe_vertex_index_endian_and_edge_factors & 0x1FFFFFFCu) +
         control_point_id * 4u;
}

#endif  // XENIA_GPU_D3D12_SHADERS_XENOS_DRAW_HLSLI_
