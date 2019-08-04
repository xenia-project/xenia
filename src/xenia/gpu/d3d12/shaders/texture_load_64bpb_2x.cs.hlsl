#include "texture_load.hlsli"

[numthreads(16, 16, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 1 uint2 guest texel.
  [branch] if (any(xe_thread_id >= xe_texture_load_size_blocks)) {
    return;
  }
  uint block_offset_guest =
      XeTextureLoadGuestBlockOffsets(xe_thread_id, 8u, 3u).x << 2u;
  uint4 blocks_01 = xe_texture_load_source.Load4(block_offset_guest);
  uint4 blocks_23 = xe_texture_load_source.Load4(block_offset_guest + 16u);
  blocks_01 = XeByteSwap(blocks_01, xe_texture_load_endianness);
  blocks_23 = XeByteSwap(blocks_23, xe_texture_load_endianness);
  uint block_offset_host = XeTextureHostLinearOffset(
      xe_thread_id << uint3(1u, 1u, 0u), xe_texture_load_size_blocks.y << 1u,
      xe_texture_load_host_pitch, 8u) + xe_texture_load_host_base;
  xe_texture_load_dest.Store4(block_offset_host, blocks_01);
  xe_texture_load_dest.Store4(block_offset_host + xe_texture_load_host_pitch,
                              blocks_23);
}
