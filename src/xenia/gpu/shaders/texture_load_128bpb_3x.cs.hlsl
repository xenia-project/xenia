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
      (XeTextureHostLinearOffset(int3(block_index) * int3(3, 3, 1),
                                 xe_texture_load_host_pitch,
                                 xe_texture_load_size_blocks.y * 3u, 16u) +
       xe_texture_load_host_offset) >> 4;
  int elements_pitch_host = xe_texture_load_host_pitch >> 4;
  int block_offset_guest =
      (XeTextureLoadGuestBlockOffset(int3(block_index), 16u, 4u) * 9) >> 4;
  uint endian = XeTextureLoadEndian32();
  int p, y, x;
  [unroll] for (p = 0; p < 2; ++p) {
    if (p) {
      block_offset_host += 3;
      if (XeTextureLoadIsTiled()) {
        // Odd block start = even block end + 16 guest bytes when tiled.
        block_offset_guest += (16 * 9) >> 4;
      }
    }
    int block_row_offset_host = block_offset_host;
    [unroll] for (y = 0; y < 3; ++y) {
      [unroll] for (x = 0; x < 3; ++x) {
        xe_texture_load_dest[block_row_offset_host + x] = XeEndianSwap32(
            xe_texture_load_source[block_offset_guest++], endian);
      }
      block_row_offset_host += elements_pitch_host;
    }
  }
}
