#include "texture_tile.hlsli"

RWByteAddressBuffer xe_texture_tile_dest : register(u0);

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 texels.
  uint2 texture_size = (xe_texture_tile_size >> uint2(0u, 16u)) & 0xFFFFu;
  uint2 texel_index = xe_thread_id.xy;
  texel_index.x <<= 2u;
  [branch] if (any(texel_index >= texture_size)) {
    return;
  }

  uint4 texel_addresses = xe_texture_tile_guest_base + XeTextureTiledOffset2D(
      texel_index, xe_texture_tile_endian_guest_pitch >> 3u, 3u);
  bool3 texels_inside = uint3(1u, 2u, 3u) + texel_index.x < texture_size.x;

  uint texels_source_offset = xe_texture_tile_host_base + texel_index.y *
                              xe_texture_tile_host_pitch + texel_index.x * 8u;
  uint4 texels = XeByteSwap64(
      xe_texture_tile_source.Load4(texels_source_offset),
      xe_texture_tile_endian_guest_pitch);
  xe_texture_tile_dest.Store2(texel_addresses.x, texels.xy);
  [branch] if (texels_inside.x) {
    xe_texture_tile_dest.Store2(texel_addresses.y, texels.zw);
    [branch] if (texels_inside.y) {
      texels = XeByteSwap64(
          xe_texture_tile_source.Load4(texels_source_offset + 16u),
          xe_texture_tile_endian_guest_pitch);
      xe_texture_tile_dest.Store2(texel_addresses.z, texels.xy);
      [branch] if (texels_inside.z) {
        xe_texture_tile_dest.Store2(texel_addresses.w, texels.zw);
      }
    }
  }
}
