#include "texture_copy.hlsli"

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 uint4 blocks.
  uint3 block_index = xe_thread_id;
  block_index.x <<= 2u;
  [branch] if (any(block_index >= xe_texture_copy_size_blocks)) {
    return;
  }
  uint4 block_offsets_guest =
      XeTextureCopyGuestBlockOffsets(block_index, 16u, 4u);
  uint4 block_0 = xe_texture_copy_source.Load4(block_offsets_guest.x);
  uint4 block_1 = xe_texture_copy_source.Load4(block_offsets_guest.y);
  uint4 block_2 = xe_texture_copy_source.Load4(block_offsets_guest.z);
  uint4 block_3 = xe_texture_copy_source.Load4(block_offsets_guest.w);
  block_0 = XeByteSwap(block_0, xe_texture_copy_endianness);
  block_1 = XeByteSwap(block_1, xe_texture_copy_endianness);
  block_2 = XeByteSwap(block_2, xe_texture_copy_endianness);
  block_3 = XeByteSwap(block_3, xe_texture_copy_endianness);
  uint block_offset_host = XeTextureHostLinearOffset(
      block_index, xe_texture_copy_size_blocks.y, xe_texture_copy_host_pitch,
      16u) + xe_texture_copy_host_base;
  uint4 block_offsets_host = uint4(0u, 16u, 32u, 48u) + block_offset_host;
  xe_texture_copy_dest.Store4(block_offsets_host.x, block_0);
  xe_texture_copy_dest.Store4(block_offsets_host.y, block_1);
  xe_texture_copy_dest.Store4(block_offsets_host.z, block_2);
  xe_texture_copy_dest.Store4(block_offsets_host.w, block_3);
}
