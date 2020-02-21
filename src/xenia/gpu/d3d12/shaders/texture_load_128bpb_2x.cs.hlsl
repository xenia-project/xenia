#include "texture_load.hlsli"

[numthreads(16, 16, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 1 uint4 guest texel.
  [branch] if (any(xe_thread_id >= xe_texture_load_size_blocks)) {
    return;
  }
  uint4 block_offsets_guest;
  block_offsets_guest.x =
      XeTextureLoadGuestBlockOffsets(xe_thread_id, 16u, 4u).x << 2u;
  block_offsets_guest.yzw = uint3(16u, 32u, 48u) + block_offsets_guest.x;
  uint4 block_0 = xe_texture_load_source.Load4(block_offsets_guest.x);
  uint4 block_1 = xe_texture_load_source.Load4(block_offsets_guest.y);
  uint4 block_2 = xe_texture_load_source.Load4(block_offsets_guest.z);
  uint4 block_3 = xe_texture_load_source.Load4(block_offsets_guest.w);
  block_0 = XeByteSwap(block_0, xe_texture_load_endianness);
  block_1 = XeByteSwap(block_1, xe_texture_load_endianness);
  block_2 = XeByteSwap(block_2, xe_texture_load_endianness);
  block_3 = XeByteSwap(block_3, xe_texture_load_endianness);
  uint block_offset_host = XeTextureHostLinearOffset(
      xe_thread_id << uint3(1u, 1u, 0u), xe_texture_load_size_blocks.y << 1u,
      xe_texture_load_host_pitch, 16u) + xe_texture_load_host_base;
  xe_texture_load_dest.Store4(block_offset_host, block_0);
  xe_texture_load_dest.Store4(block_offset_host + 16u, block_1);
  block_offset_host += xe_texture_load_host_pitch;
  xe_texture_load_dest.Store4(block_offset_host, block_2);
  xe_texture_load_dest.Store4(block_offset_host + 16u, block_3);
}
