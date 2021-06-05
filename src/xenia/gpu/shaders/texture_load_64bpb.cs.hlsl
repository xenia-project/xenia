#include "texture_load.hlsli"

Buffer<uint4> xe_texture_load_source : register(t0);
RWBuffer<uint4> xe_texture_load_dest : register(u0);

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 blocks.
  uint3 block_index = xe_thread_id << uint3(2, 0, 0);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int block_offset_host =
      (XeTextureHostLinearOffset(int3(block_index), xe_texture_load_host_pitch,
                                 xe_texture_load_size_blocks.y, 8u) +
       xe_texture_load_host_offset) >> 4;
  int block_offset_guest =
      XeTextureLoadGuestBlockOffset(int3(block_index), 8u, 3u) >> 4;
  uint endian = XeTextureLoadEndian32();
  xe_texture_load_dest[block_offset_host] =
      XeEndianSwap32(xe_texture_load_source[block_offset_guest], endian);
  ++block_offset_host;
  // Odd 2 blocks = even 2 blocks + 32 bytes when tiled.
  block_offset_guest += XeTextureLoadIsTiled() ? 2 : 1;
  xe_texture_load_dest[block_offset_host] =
      XeEndianSwap32(xe_texture_load_source[block_offset_guest], endian);
}
