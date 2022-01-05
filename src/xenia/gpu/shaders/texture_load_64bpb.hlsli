#include "texture_load.hlsli"

Buffer<uint4> xe_texture_load_source : register(t0);
RWBuffer<uint4> xe_texture_load_dest : register(u0);

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 blocks.
  uint3 block_index = xe_thread_id << uint3(2u, 0u, 0u);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int block_offset_host =
      (XeTextureHostLinearOffset(int3(block_index), xe_texture_load_host_pitch,
                                 xe_texture_load_size_blocks.y, 8u) +
       xe_texture_load_host_offset) >> 4u;
  uint block_offset_guest =
      XeTextureLoadGuestBlockOffset(block_index, 8u, 3u) >> 4u;
  uint endian = XeTextureLoadEndian32();
  xe_texture_load_dest[block_offset_host] =
      XeEndianSwap32(xe_texture_load_source[block_offset_guest], endian);
  ++block_offset_host;
  block_offset_guest +=
      XeTextureLoadRightConsecutiveBlocksOffset(block_index.x, 3u) >> 4u;
  xe_texture_load_dest[block_offset_host] =
      XeEndianSwap32(xe_texture_load_source[block_offset_guest], endian);
}
