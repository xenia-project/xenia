#include "texture_tile.hlsli"

RWByteAddressBuffer xe_texture_tile_dest : register(u0);

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 texels.
  uint2 texture_size = XeTextureTileSizeScaled();
  uint2 texel_index = xe_thread_id.xy;
  texel_index.x *= 4u;
  [branch] if (any(texel_index >= texture_size)) {
    return;
  }

  uint4 texel_addresses = XeTextureTileGuestAddress(texel_index, 2u);
  bool3 texels_inside = uint3(1u, 2u, 3u) + texel_index.x < texture_size.x;

  uint endian = XeTextureTileEndian();
  uint texels_host_offset = xe_texture_tile_host_base + texel_index.y *
                            xe_texture_tile_host_pitch + texel_index.x * 8u;
  uint4 texels_host = xe_texture_tile_source.Load4(texels_host_offset);
  uint2 texels = XeByteSwap(
      ((texels_host.xz >> 6u) & 1023u) | ((texels_host.xz >> 21u) << 10u) |
          ((texels_host.yw >> 5u) << 21u), endian);
  xe_texture_tile_dest.Store(texel_addresses.x, texels.x);
  [branch] if (texels_inside.x) {
    xe_texture_tile_dest.Store(texel_addresses.y, texels.y);
    [branch] if (texels_inside.y) {
      texels_host = xe_texture_tile_source.Load4(texels_host_offset + 16u);
      texels = XeByteSwap(
          ((texels_host.xz >> 6u) & 1023u) | ((texels_host.xz >> 21u) << 10u) |
              ((texels_host.yw >> 5u) << 21u), endian);
      xe_texture_tile_dest.Store(texel_addresses.z, texels.x);
      [branch] if (texels_inside.z) {
        xe_texture_tile_dest.Store(texel_addresses.w, texels.y);
      }
    }
  }
}
