#ifndef XENIA_GPU_D3D12_SHADERS_EDRAM_LOAD_STORE_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_EDRAM_LOAD_STORE_HLSLI_

// Root constants.
cbuffer XeEdramLoadStoreConstants : register(b0) {
  uint xe_edram_rt_color_depth_offset;
  uint xe_edram_rt_color_depth_pitch;
  uint xe_edram_rt_stencil_offset;
  uint xe_edram_rt_stencil_pitch;
  // 0:10 - EDRAM base in tiles.
  // 11 - log2(vertical sample count), 0 for 1x AA, 1 for 2x/4x AA.
  // 12 - log2(horizontal sample count), 0 for 1x/2x AA, 1 for 4x AA.
  // 13 - whether 2x resolution scale is used.
  // 14 - whether to apply the hack and duplicate the top/left
  //      half-row/half-column to reduce the impact of half-pixel offset with
  //      2x resolution scale.
  // 15 - whether it's a depth render target.
  // 16: - EDRAM pitch in tiles.
  uint xe_edram_base_samples_2x_depth_pitch;
};

RWByteAddressBuffer xe_edram_load_store_dest : register(u0);
ByteAddressBuffer xe_edram_load_store_source : register(t0);

uint2 XeEdramSampleCountLog2() {
  return (xe_edram_base_samples_2x_depth_pitch >> uint2(12u, 11u)) & 1u;
}

uint XeEdramScaleLog2() {
  return (xe_edram_base_samples_2x_depth_pitch >> 13u) & 1u;
}

uint XeEdramOffset32bpp(uint2 tile_index, uint2 tile_sample_index,
                        uint2 scaled_2x_pixel_index = uint2(0u, 0u)) {
  if (xe_edram_base_samples_2x_depth_pitch & (1u << 15u)) {
    // Depth render targets apparently have samples 0:39 and 40:79 swapped -
    // affects loading depth to EDRAM via color aliasing in GTA IV and Halo 3.
    tile_sample_index.x += 40u * uint(tile_sample_index.x < 40u) -
                           40u * uint(tile_sample_index.x >= 40u);
  }
  uint offset = ((xe_edram_base_samples_2x_depth_pitch & 2047u) +
                 tile_index.y * (xe_edram_base_samples_2x_depth_pitch >> 16u) +
                 tile_index.x) * 5120u + tile_sample_index.y * 320u +
                tile_sample_index.x * 4u;
  uint resolution_scale_log2 = XeEdramScaleLog2();
  scaled_2x_pixel_index &= resolution_scale_log2;
  return (offset << (resolution_scale_log2 * 2u)) +
         scaled_2x_pixel_index.y * 8u + scaled_2x_pixel_index.x * 4u;
}

// Instead of individual tiles, this works on two consecutive tiles, the first
// one containing the top 80x8 samples, and the second one containing the bottom
// 80x8 samples.
uint XeEdramOffset64bpp(uint2 tile_pair_index, uint2 tile_pair_sample_index,
                        uint2 scaled_2x_pixel_index = uint2(0u, 0u)) {
  uint offset = ((xe_edram_base_samples_2x_depth_pitch & 2047u) +
                 tile_pair_index.y *
                 (xe_edram_base_samples_2x_depth_pitch >> 16u) +
                 (tile_pair_index.x << 1u)) * 5120u +
                tile_pair_sample_index.y * 640u + tile_pair_sample_index.x * 8u;
  uint resolution_scale_log2 = XeEdramScaleLog2();
  scaled_2x_pixel_index &= resolution_scale_log2;
  return (offset << (resolution_scale_log2 * 2u)) +
         scaled_2x_pixel_index.y * 16u + scaled_2x_pixel_index.x * 8u;
}

#endif  // XENIA_GPU_D3D12_SHADERS_EDRAM_LOAD_STORE_HLSLI_
