#include "texture_load.hlsli"

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 uint2 blocks.
  uint3 block_index = xe_thread_id;
  block_index.x <<= 2u;
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  uint4 block_offsets_guest =
      XeTextureLoadGuestBlockOffsets(block_index, 8u, 3u);
  uint4 blocks_01 = uint4(xe_texture_load_source.Load2(block_offsets_guest.x),
                          xe_texture_load_source.Load2(block_offsets_guest.y));
  uint4 blocks_23 = uint4(xe_texture_load_source.Load2(block_offsets_guest.z),
                          xe_texture_load_source.Load2(block_offsets_guest.w));
  blocks_01 = XeByteSwap(blocks_01, xe_texture_load_endianness);
  blocks_23 = XeByteSwap(blocks_23, xe_texture_load_endianness);
  uint block_offset_host = XeTextureHostLinearOffset(
      block_index, xe_texture_load_size_blocks.y, xe_texture_load_host_pitch,
      8u) + xe_texture_load_host_base;
  xe_texture_load_dest.Store4(block_offset_host, blocks_01);
  xe_texture_load_dest.Store4(block_offset_host + 16u, blocks_23);
}
