#ifndef XENIA_GPU_D3D12_SHADERS_EDRAM_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_EDRAM_HLSLI_

#include "pixel_formats.hlsli"

#define kXenosMsaaSamples_1X 0u
#define kXenosMsaaSamples_2X 1u
#define kXenosMsaaSamples_4X 2u

uint XeEdramOffsetInts(uint2 pixel_index, uint base_tiles, uint pitch_tiles,
                       uint msaa_samples, bool is_depth, uint format_ints_log2,
                       uint pixel_sample_index, uint2 resolution_scale) {
  uint2 rt_sample_index =
      pixel_index <<
      uint2(msaa_samples >= uint2(kXenosMsaaSamples_4X, kXenosMsaaSamples_2X));
  rt_sample_index += (pixel_sample_index >> uint2(1u, 0u)) & 1u;
  // For now, while the actual storage of 64bpp render targets in comparison to
  // 32bpp is not known, storing 40x16 64bpp samples per tile for simplicity of
  // addressing in different scenarios.
  uint2 tile_size_at_32bpp = uint2(80u, 16u) * resolution_scale;
  uint2 tile_size_samples = tile_size_at_32bpp >> uint2(format_ints_log2, 0u);
  uint2 tile_offset_xy = rt_sample_index / tile_size_samples;
  base_tiles += tile_offset_xy.y * pitch_tiles + tile_offset_xy.x;
  rt_sample_index -= tile_offset_xy * tile_size_samples;
  if (is_depth) {
    uint tile_width_half = tile_size_samples.x >> 1u;
    rt_sample_index.x =
        uint(int(rt_sample_index.x) +
             ((rt_sample_index.x >= tile_width_half) ? -int(tile_width_half)
                                                     : int(tile_width_half)));
  }
  return base_tiles * (tile_size_at_32bpp.x * tile_size_at_32bpp.y) +
         ((rt_sample_index.y * tile_size_samples.x + rt_sample_index.x) <<
          format_ints_log2);
}

#endif  // XENIA_GPU_D3D12_SHADERS_EDRAM_HLSLI_
