#ifndef XENIA_GPU_D3D12_SHADERS_EDRAM_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_EDRAM_HLSLI_

#include "pixel_formats.hlsli"

#define kXenosMsaaSamples_1X 0u
#define kXenosMsaaSamples_2X 1u
#define kXenosMsaaSamples_4X 2u

// tile_size_scale is for "simplified" resolution scaling cases (like host depth
// storing) where the user is aware of the MSAA sample count, thus can use host
// multisampled pixels within guest multisampled pixels, not host samples within
// guest samples unlike in the usual case.
uint XeEdramOffsetInts(uint2 pixel_index, uint base_tiles, uint pitch_tiles,
                       uint msaa_samples, bool is_depth, bool format_ints_log2,
                       uint sample_index = 0u, uint tile_size_scale = 1u) {
  pixel_index <<=
      uint2(msaa_samples >= uint2(kXenosMsaaSamples_4X, kXenosMsaaSamples_2X));
  pixel_index += (sample_index >> uint2(1u, 0u)) & 1u;
  uint2 tile_size = uint2(80u, 16u) * tile_size_scale;
  uint2 tile_offset_xy = pixel_index / tile_size;
  base_tiles +=
      tile_offset_xy.y * pitch_tiles + (tile_offset_xy.x << format_ints_log2);
  pixel_index -= tile_offset_xy * tile_size;
  if (is_depth) {
    uint tile_width_half = tile_size.x >> 1u;
    pixel_index.x =
        uint(int(pixel_index.x) +
             ((pixel_index.x >= tile_width_half) ? -int(tile_width_half)
                                                 : int(tile_width_half)));
  }
  return base_tiles * (tile_size.x * tile_size.y) +
         ((pixel_index.y * tile_size.x + pixel_index.x) << format_ints_log2);
}

#endif  // XENIA_GPU_D3D12_SHADERS_EDRAM_HLSLI_
