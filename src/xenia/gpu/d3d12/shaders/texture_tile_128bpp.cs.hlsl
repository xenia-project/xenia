#include "texture_tile.hlsli"

RWByteAddressBuffer xe_texture_tile_dest : register(u0);

[numthreads(32, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 1 texel (no benefit from reading 4 texels here).
  uint2 texture_size = XeTextureTileSizeScaled();
  [branch] if (any(xe_thread_id.xy >= texture_size)) {
    return;
  }
  uint texel_address = XeTextureTileGuestAddress(xe_thread_id.xy, 4u).x;
  uint texel_source_offset = xe_texture_tile_host_base + xe_thread_id.y *
                             xe_texture_tile_host_pitch + xe_thread_id.x * 16u;
  uint4 texel = XeByteSwap128(
      xe_texture_tile_source.Load4(texel_source_offset), XeTextureTileEndian());
  xe_texture_tile_dest.Store4(texel_address, texel);
}
