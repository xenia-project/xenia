#include "byte_swap.hlsli"
#include "edram_load_store.hlsli"
#include "texture_address.hlsli"

[numthreads(20, 16, 1)]
void main(uint3 xe_group_id : SV_GroupID,
          uint3 xe_group_thread_id : SV_GroupThreadID,
          uint3 xe_thread_id : SV_DispatchThreadID) {
  // Check if not outside of the destination texture completely.
  uint4 copy_rect;
  copy_rect.xz = xe_edram_tile_sample_rect & 0xFFFFu;
  copy_rect.yw = xe_edram_tile_sample_rect >> 16u;
  uint2 texel_index = xe_thread_id.xy;
  texel_index.x *= 4u;
  [branch] if (any(texel_index < copy_rect.xy) ||
               any(texel_index >= copy_rect.zw)) {
    return;
  }

  // Get the samples from the EDRAM buffer.
  // XY - log2(pixel size), ZW - selected sample offset.
  uint4 sample_info =
      (xe_edram_tile_sample_dest_info.xxxx >> uint4(15u, 14u, 17u, 16u)) & 1u;
  uint2 edram_tile_quarter =
      uint2(uint2(10u, 8u) <= xe_group_thread_id.xy) * sample_info.xy;
  uint edram_offset = XeEDRAMOffset64bpp(
      (xe_group_id.xy << sample_info.xy) + edram_tile_quarter,
      (xe_group_thread_id.xy - edram_tile_quarter * uint2(10u, 8u)) <<
      (sample_info.xy + uint2(2u, 0u)) + sample_info.zw);
  // Loaded with the first 2 pixels at 1x and 2x, or the first 1 pixel at 4x.
  uint4 pixels_01 = xe_edram_load_store_source.Load4(edram_offset);
  // Loaded with the second 2 pixels at 1x and 2x, or the second 1 pixel at 4x.
  uint4 pixels_23 = xe_edram_load_store_source.Load4(edram_offset + 16u);
  [branch] if (sample_info.x != 0u) {
    // Rather than 4 pixels, at 4x, we only have 2 - in xy of each variable
    // rather than in xyzw of pixels_01. Combine and load 2 more.
    pixels_01.zw = pixels_23.xy;
    pixels_23.xy = xe_edram_load_store_source.Load2(edram_offset + 32u);
    pixels_23.zw = xe_edram_load_store_source.Load2(edram_offset + 48u);
  }

  if ((xe_edram_tile_sample_dest_info >> 21u) != 0u) {
    // Swap red and blue - all 64bpp formats where this is possible are
    // 16:16:16:16.
    pixels_01 = (pixels_01 & 0xFFFF0000u) | (pixels_01.yxwz & 0xFFFFu);
    pixels_23 = (pixels_23 & 0xFFFF0000u) | (pixels_23.yxwz & 0xFFFFu);
  }

  // Tile the pixels to the shared memory.
  pixels_01 = XeByteSwap64(pixels_01, xe_edram_tile_sample_dest_info >> 18u);
  pixels_23 = XeByteSwap64(pixels_23, xe_edram_tile_sample_dest_info >> 18u);
  uint4 texel_addresses =
      xe_edram_tile_sample_dest_base +
      XeTextureTiledOffset2D(texel_index - copy_rect.xy,
                             xe_edram_tile_sample_dest_info & 16383u, 3u);
  xe_edram_load_store_dest.Store2(texel_addresses.x, pixels_01.xy);
  bool3 texels_in_rect = uint3(1u, 2u, 3u) + texel_index.x < copy_rect.z;
  [branch] if (texels_in_rect.x) {
    xe_edram_load_store_dest.Store2(texel_addresses.y, pixels_01.zw);
    [branch] if (texels_in_rect.y) {
      xe_edram_load_store_dest.Store2(texel_addresses.z, pixels_23.xy);
      [branch] if (texels_in_rect.z) {
        xe_edram_load_store_dest.Store2(texel_addresses.w, pixels_23.zw);
      }
    }
  }
}
