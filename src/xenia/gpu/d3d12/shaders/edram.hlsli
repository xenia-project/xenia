#ifndef XENIA_GPU_D3D12_SHADERS_EDRAM_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_EDRAM_HLSLI_

#include "pixel_formats.hlsli"

#define kXenosMsaaSamples_1X 0u
#define kXenosMsaaSamples_2X 1u
#define kXenosMsaaSamples_4X 2u

uint XeEdramOffsetInts(uint2 pixel_index, uint base_tiles, uint pitch_tiles,
                       uint msaa_samples, bool is_depth, bool format_ints_log2,
                       uint sample_index = 0u) {
  pixel_index <<=
      uint2(msaa_samples >= uint2(kXenosMsaaSamples_4X, kXenosMsaaSamples_2X));
  pixel_index += (sample_index >> uint2(1u, 0u)) & 1u;
  uint2 tile_offset_xy = uint2(pixel_index.x / 80u, pixel_index.y >> 4u);
  base_tiles +=
      tile_offset_xy.y * pitch_tiles + (tile_offset_xy.x << format_ints_log2);
  pixel_index -= tile_offset_xy * uint2(80u, 16u);
  if (is_depth) {
    pixel_index.x =
        uint(int(pixel_index.x) + ((pixel_index.x >= 40u) ? -40 : 40));
  }
  return base_tiles * 1280u +
         ((pixel_index.y * 80u + pixel_index.x) << format_ints_log2);
}

#endif  // XENIA_GPU_D3D12_SHADERS_EDRAM_HLSLI_
