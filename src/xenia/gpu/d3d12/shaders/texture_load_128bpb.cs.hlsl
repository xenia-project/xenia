#include "texture_load.hlsli"

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 uint4 blocks.
  uint3 block_index = xe_thread_id;
  block_index.x <<= 2u;
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  uint4 block_offsets_guest =
      XeTextureLoadGuestBlockOffsets(block_index, 16u, 4u);
  uint4 block_0 = xe_texture_load_source.Load4(block_offsets_guest.x);
  uint4 block_1 = xe_texture_load_source.Load4(block_offsets_guest.y);
  uint4 block_2 = xe_texture_load_source.Load4(block_offsets_guest.z);
  uint4 block_3 = xe_texture_load_source.Load4(block_offsets_guest.w);
  block_0 = XeByteSwap(block_0, xe_texture_load_endianness);
  block_1 = XeByteSwap(block_1, xe_texture_load_endianness);
  block_2 = XeByteSwap(block_2, xe_texture_load_endianness);
  block_3 = XeByteSwap(block_3, xe_texture_load_endianness);
  uint4 block_offsets_host;
  block_offsets_host.x = XeTextureHostLinearOffset(
      block_index, xe_texture_load_size_blocks.y, xe_texture_load_host_pitch,
      16u) + xe_texture_load_host_base;
  block_offsets_host.yzw = uint3(16u, 32u, 48u) + block_offsets_host.x;
  xe_texture_load_dest.Store4(block_offsets_host.x, block_0);
  xe_texture_load_dest.Store4(block_offsets_host.y, block_1);
  xe_texture_load_dest.Store4(block_offsets_host.z, block_2);
  xe_texture_load_dest.Store4(block_offsets_host.w, block_3);
}
