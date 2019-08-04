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
  uint4 texels = xe_texture_tile_source.Load4(
      xe_texture_tile_host_base + texel_index.y * xe_texture_tile_host_pitch +
      texel_index.x * 4u);
  texels = XeByteSwap(texels, XeTextureTileEndian());
  uint4 texel_addresses = XeTextureTileGuestAddress(texel_index, 2u);
  xe_texture_tile_dest.Store(texel_addresses.x, texels.x);
  bool3 texels_inside = uint3(1u, 2u, 3u) + texel_index.x < texture_size.x;
  [branch] if (texels_inside.x) {
    xe_texture_tile_dest.Store(texel_addresses.y, texels.y);
    [branch] if (texels_inside.y) {
      xe_texture_tile_dest.Store(texel_addresses.z, texels.z);
      [branch] if (texels_inside.z) {
        xe_texture_tile_dest.Store(texel_addresses.w, texels.w);
      }
    }
  }
}
