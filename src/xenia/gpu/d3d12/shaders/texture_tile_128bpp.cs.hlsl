#include "texture_tile.hlsli"

RWByteAddressBuffer xe_texture_tile_dest : register(u0);

[numthreads(32, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 1 texel (no benefit from reading 4 texels here).
  uint2 texture_size = (xe_texture_tile_size >> uint2(0u, 16u)) & 0xFFFFu;
  [branch] if (any(xe_thread_id.xy >= texture_size)) {
    return;
  }
  uint texel_address = xe_texture_tile_guest_base + XeTextureTiledOffset2D(
      xe_thread_id.xy, xe_texture_tile_endian_format_guest_pitch >> 9u, 4u).x;
  uint texel_source_offset = xe_texture_tile_host_base + xe_thread_id.y *
                             xe_texture_tile_host_pitch + xe_thread_id.x * 16u;
  uint4 texel = XeByteSwap128(
      xe_texture_tile_source.Load4(texel_source_offset),
      xe_texture_tile_endian_format_guest_pitch);
  xe_texture_tile_dest.Store4(texel_address, texel);
}
