#include "texture_load.hlsli"

Buffer<uint4> xe_texture_load_source : register(t0);
RWBuffer<uint4> xe_texture_load_dest : register(u0);

[numthreads(16, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 2 blocks.
  uint3 block_index = xe_thread_id << uint3(1, 0, 0);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int block_offset_host =
      (XeTextureHostLinearOffset(int3(block_index), xe_texture_load_host_pitch,
                                 xe_texture_load_size_blocks.y, 16u) +
       xe_texture_load_host_offset) >> 4;
  int block_offset_guest =
      XeTextureLoadGuestBlockOffset(int3(block_index), 16u, 4u) >> 4;
  uint endian = XeTextureLoadEndian32();
  xe_texture_load_dest[block_offset_host] =
      XeEndianSwap32(xe_texture_load_source[block_offset_guest], endian);
  ++block_offset_host;
  // Odd block = even block + 32 bytes when tiled.
  block_offset_guest += XeTextureLoadIsTiled() ? 2 : 1;
  xe_texture_load_dest[block_offset_host] =
      XeEndianSwap32(xe_texture_load_source[block_offset_guest], endian);
}
