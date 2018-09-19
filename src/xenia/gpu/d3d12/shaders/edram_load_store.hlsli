#ifndef XENIA_GPU_D3D12_SHADERS_EDRAM_LOAD_STORE_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_EDRAM_LOAD_STORE_HLSLI_

// Root constants.
cbuffer XeEDRAMLoadStoreConstants : register(b0) {
  uint4 xe_edram_load_store_constants;
  // 0:10 - EDRAM base in tiles.
  // 11 - whether it's a depth render target.
  // 12: - EDRAM pitch in tiles.
  uint xe_edram_base_depth_pitch;
};

// For loading and storing render targets.
#define xe_edram_rt_color_depth_offset (xe_edram_load_store_constants.x)
#define xe_edram_rt_color_depth_pitch (xe_edram_load_store_constants.y)
#define xe_edram_rt_stencil_offset (xe_edram_load_store_constants.z)
#define xe_edram_rt_stencil_pitch (xe_edram_load_store_constants.w)

// For single sample resolving.
// Left/top of the copied region (relative to EDRAM base) in the lower 16 bits,
// right/bottom in the upper.
#define xe_edram_tile_sample_rect (xe_edram_load_store_constants.xy)
#define xe_edram_tile_sample_dest_base (xe_edram_load_store_constants.z)
// 0:13 - destination pitch.
// 14 - log2(vertical sample count), 0 for 1x AA, 1 for 2x/4x AA.
// 15 - log2(horizontal sample count), 0 for 1x/2x AA, 1 for 4x AA.
// 16:17 - sample to load (16 - vertical index, 17 - horizontal index).
// 18:20 - destination endianness.
// 21:31 - BPP-specific info for swapping red/blue, 0 if not swapping.
//   For 32 bits per sample:
//     21:25 - red/blue bit depth.
//     26:30 - blue offset.
//   For 64 bits per sample, it's 1 if need to swap 0:15 and 32:47.
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

uint XeEDRAMOffset32bpp(uint2 tile_index, uint2 tile_sample_index) {
  if (xe_edram_base_depth_pitch & (1u << 11u)) {
    // Depth render targets apparently have samples 0:39 and 40:79 swapped -
    // affects loading depth to EDRAM via color aliasing in GTA IV and Halo 3.
    tile_sample_index.x += 40u * uint(tile_sample_index.x < 40u) -
                           40u * uint(tile_sample_index.x >= 40u);
  }
  return ((xe_edram_base_depth_pitch & 2047u) +
          tile_index.y * (xe_edram_base_depth_pitch >> 12u) + tile_index.x) *
         5120u + tile_sample_index.y * 320u + tile_sample_index.x * 4u;
}

// Instead of individual tiles, this works on two consecutive tiles, the first
// one containing the top 80x8 samples, and the second one containing the bottom
// 80x8 samples.
uint XeEDRAMOffset64bpp(uint2 tile_pair_index, uint2 tile_pair_sample_index) {
  return ((xe_edram_base_depth_pitch & 2047u) +
          tile_pair_index.y * (xe_edram_base_depth_pitch >> 12u) +
          (tile_pair_index.x << 1u)) * 5120u +
         tile_pair_sample_index.y * 640u + tile_pair_sample_index.x * 8u;
}

#endif  // XENIA_GPU_D3D12_SHADERS_EDRAM_LOAD_STORE_HLSLI_
