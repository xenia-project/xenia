#include "texture_copy.hlsli"

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 DXT3A blocks to 4 DXT3 blocks with zero color.
  uint3 block_index = xe_thread_id;
  block_index.x <<= 2u;
  [branch] if (any(block_index >= xe_texture_copy_size_blocks)) {
    return;
  }
  uint4 block_offsets_guest =
      XeTextureCopyGuestBlockOffsets(block_index, 8u, 3u);
  uint4 blocks_01 = uint4(xe_texture_copy_source.Load2(block_offsets_guest.x),
                          xe_texture_copy_source.Load2(block_offsets_guest.y));
  uint4 blocks_23 = uint4(xe_texture_copy_source.Load2(block_offsets_guest.z),
                          xe_texture_copy_source.Load2(block_offsets_guest.w));
  blocks_01 = XeByteSwap(blocks_01, xe_texture_copy_endianness);
  blocks_23 = XeByteSwap(blocks_23, xe_texture_copy_endianness);
  uint block_offset_host = XeTextureHostLinearOffset(
      block_index, xe_texture_copy_size_blocks.y, xe_texture_copy_host_pitch,
      16u) + xe_texture_copy_host_base;
  xe_texture_copy_dest.Store4(block_offset_host, uint4(blocks_01.xy, 0u, 0u));
  xe_texture_copy_dest.Store4(block_offset_host + 16u,
                              uint4(blocks_01.zw, 0u, 0u));
  xe_texture_copy_dest.Store4(block_offset_host + 32u,
                              uint4(blocks_23.xy, 0u, 0u));
  xe_texture_copy_dest.Store4(block_offset_host + 48u,
                              uint4(blocks_23.zw, 0u, 0u));
}
