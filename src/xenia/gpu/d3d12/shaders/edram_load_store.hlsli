#ifndef XENIA_GPU_D3D12_SHADERS_EDRAM_LOAD_STORE_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_EDRAM_LOAD_STORE_HLSLI_

// Root constants.
cbuffer XeEDRAMLoadStoreConstants : register(b0) {
  uint4 xe_edram_load_store_constants;
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

// For loading and storing render targets.
#define xe_edram_rt_color_depth_offset (xe_edram_load_store_constants.x)
#define xe_edram_rt_color_depth_pitch (xe_edram_load_store_constants.y)
#define xe_edram_rt_stencil_offset (xe_edram_load_store_constants.z)
#define xe_edram_rt_stencil_pitch (xe_edram_load_store_constants.w)

// For single sample resolving.
// 0:11 - resolve area width/height in pixels.
// 12:16 - offset in the destination texture (only up to 31 - assuming 32*n is
//         pre-applied to the base pointer).
// 17:19 - Z in destination 3D texture (in .x).
// 20:31 - left/top of the copied region (relative to EDRAM base).
#define xe_edram_tile_sample_dimensions (xe_edram_load_store_constants.xy)
#define xe_edram_tile_sample_dest_base (xe_edram_load_store_constants.z)
// 0:8 - align(destination pitch, 32) / 32.
// 9:17 - align(destination height, 32) / 32 for 3D, 0 for 2D.
// 18:19 - sample to load (18 - vertical index, 19 - horizontal index).
// 20:22 - destination endianness.
// 23 - whether to swap red and blue.
// 24:27 - source format (for red/blue swapping).
#define xe_edram_tile_sample_dest_info (xe_edram_load_store_constants.w)

// For clearing.
// Left/top of the cleared region (relative to EDRAM base) in the lower 16 bits,
// right/bottom in the upper, in samples.
#define xe_edram_clear_rect (xe_edram_load_store_constants.xy)
#define xe_edram_clear_color32 (xe_edram_load_store_constants.z)
#define xe_edram_clear_color64 (xe_edram_load_store_constants.zw)
#define xe_edram_clear_depth24 (xe_edram_load_store_constants.z)
#define xe_edram_clear_depth32 (xe_edram_load_store_constants.w)

#ifndef XE_EDRAM_WRITE_ONLY
ByteAddressBuffer xe_edram_load_store_source : register(t0);
#endif
RWByteAddressBuffer xe_edram_load_store_dest : register(u0);

uint2 XeEDRAMSampleCountLog2() {
  return (xe_edram_base_samples_2x_depth_pitch >> uint2(12u, 11u)) & 1u;
}

uint XeEDRAMScaleLog2() {
  return (xe_edram_base_samples_2x_depth_pitch >> 13u) & 1u;
}

// X - log2(horizontal sample count)
// Y - log2(vertical sample count)
// Z - log2(resolution scale)
// W - whether to apply the host pixel duplication hack for 2x resolution scale
//     resolves
uint4 XeEDRAMSampleCountAndScaleInfo() {
  return
      (xe_edram_base_samples_2x_depth_pitch >> uint4(12u, 11u, 13u, 14u)) & 1u;
}

uint XeEDRAMOffset32bpp(uint2 tile_index, uint2 tile_sample_index,
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
  uint resolution_scale_log2 = XeEDRAMScaleLog2();
  scaled_2x_pixel_index &= resolution_scale_log2;
  return (offset << (resolution_scale_log2 * 2u)) +
         scaled_2x_pixel_index.y * 8u + scaled_2x_pixel_index.x * 4u;
}

// Instead of individual tiles, this works on two consecutive tiles, the first
// one containing the top 80x8 samples, and the second one containing the bottom
// 80x8 samples.
uint XeEDRAMOffset64bpp(uint2 tile_pair_index, uint2 tile_pair_sample_index,
                        uint2 scaled_2x_pixel_index = uint2(0u, 0u)) {
  uint offset = ((xe_edram_base_samples_2x_depth_pitch & 2047u) +
                 tile_pair_index.y *
                 (xe_edram_base_samples_2x_depth_pitch >> 16u) +
                 (tile_pair_index.x << 1u)) * 5120u +
                tile_pair_sample_index.y * 640u + tile_pair_sample_index.x * 8u;
  uint resolution_scale_log2 = XeEDRAMScaleLog2();
  scaled_2x_pixel_index &= resolution_scale_log2;
  return (offset << (resolution_scale_log2 * 2u)) +
         scaled_2x_pixel_index.y * 16u + scaled_2x_pixel_index.x * 8u;
}

#endif  // XENIA_GPU_D3D12_SHADERS_EDRAM_LOAD_STORE_HLSLI_
