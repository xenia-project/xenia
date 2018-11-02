#include "byte_swap.hlsli"
#include "edram_load_store.hlsli"
#include "texture_address.hlsli"

[numthreads(20, 16, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
  // Check if not outside of the destination texture completely.
  uint2 source_offset = xe_edram_tile_sample_dimensions >> 17u;
  [branch] if (any(xe_thread_id.xy * uint2(4u, 1u) < source_offset.xy)) {
    return;
  }
  uint2 texel_index = xe_thread_id.xy - source_offset;
  texel_index.x *= 4u;
  uint2 copy_size = xe_edram_tile_sample_dimensions & 0xFFFu;
  [branch] if (any(texel_index >= copy_size)) {
    return;
  }

  // Get the samples from the EDRAM buffer.
  // XY - log2(pixel size), ZW - selected sample offset.
  uint4 sample_info =
      (xe_edram_tile_sample_dest_info.xxxx >> uint4(15u, 14u, 17u, 16u)) & 1u;
  uint2 edram_tile_quarter =
      uint2(uint2(10u, 8u) <= xe_group_thread_id.xy) * sample_info.xy;
  uint edram_offset = XeEDRAMOffset32bpp(
      (xe_group_id.xy << sample_info.xy) + edram_tile_quarter,
      ((xe_group_thread_id.xy - edram_tile_quarter * uint2(10u, 8u)) <<
       (sample_info.xy + uint2(2u, 0u))) + sample_info.zw);
  // At 1x and 2x, this contains samples of 4 pixels. At 4x, this contains
  // samples of 2, need to load 2 more.
  uint4 pixels = xe_edram_load_store_source.Load4(edram_offset);
  [branch] if (sample_info.x != 0u) {
    pixels.xy = pixels.xz;
    pixels.zw = xe_edram_load_store_source.Load3(edram_offset + 16u).xz;
  }

  uint red_blue_swap = xe_edram_tile_sample_dest_info >> 21u;
  if (red_blue_swap != 0u) {
    uint red_mask = (1u << (red_blue_swap & 31u)) - 1u;
    // No need to be ready for a long shift Barney, it's just 16 or 20.
    uint blue_shift = red_blue_swap >> 5u;
    uint blue_mask = red_mask << blue_shift;
    pixels = (pixels & ~(red_mask | blue_mask)) |
             ((pixels & red_mask) << blue_shift) |
             ((pixels >> blue_shift) & red_mask);
  }

  // Tile the pixels to the shared memory.
  pixels = XeByteSwap(pixels, xe_edram_tile_sample_dest_info >> 18u);
  uint4 texel_addresses =
      xe_edram_tile_sample_dest_base +
      XeTextureTiledOffset2D(
          ((xe_edram_tile_sample_dimensions >> 12u) & 31u) + texel_index,
          xe_edram_tile_sample_dest_info & 16383u, 2u);
  xe_edram_load_store_dest.Store(texel_addresses.x, pixels.x);
  bool3 texels_in_rect = uint3(1u, 2u, 3u) + texel_index.x < copy_size.x;
  [branch] if (texels_in_rect.x) {
    xe_edram_load_store_dest.Store(texel_addresses.y, pixels.y);
    [branch] if (texels_in_rect.y) {
      xe_edram_load_store_dest.Store(texel_addresses.z, pixels.z);
      [branch] if (texels_in_rect.z) {
        xe_edram_load_store_dest.Store(texel_addresses.w, pixels.w);
      }
    }
  }
}
