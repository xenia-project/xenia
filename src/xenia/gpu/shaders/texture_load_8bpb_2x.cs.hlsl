#include "texture_load.hlsli"

Buffer<uint4> xe_texture_load_source : register(t0);
RWBuffer<uint4> xe_texture_load_dest : register(u0);

[numthreads(2, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 16 blocks.
  uint3 block_index = xe_thread_id << uint3(4, 0, 0);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int block_offset_host =
      (XeTextureHostLinearOffset(int3(block_index) << int3(1, 1, 0),
                                 xe_texture_load_host_pitch,
                                 xe_texture_load_size_blocks.y << 1, 1u) +
       xe_texture_load_host_offset) >> 4;
  int elements_pitch_host = xe_texture_load_host_pitch >> 4;
  int block_offset_guest =
      XeTextureLoadGuestBlockOffset(int3(block_index), 1u, 0u) >> (4 - 2);
  int i;
  [unroll] for (i = 0; i < 2; ++i) {
    if (i) {
      ++block_offset_host;
      // Odd 8 blocks = even 8 blocks + 64 guest bytes when tiled.
      block_offset_guest += XeTextureLoadIsTiled() ? (64 * 4 / 16) : 2;
    }
    // Bits 0:7 - top-left of each.
    // Bits 8:15 - top-right of each.
    // Bits 16:23 - bottom-left of each.
    // Bits 24:31 - bottom-right of each.
    uint4 blocks_0123 = xe_texture_load_source[block_offset_guest];
    uint4 blocks_4567 = xe_texture_load_source[block_offset_guest + 1];
    // Top-left and top-right host pixel from bits 0:15.
    // X = 0[0:15] | (1[0:15] << 16)
    // Y = 2[0:15] | (3[0:15] << 16)
    // Z = 4[0:15] | (5[0:15] << 16)
    // W = 6[0:15] | (7[0:15] << 16)
    xe_texture_load_dest[block_offset_host] =
        (uint4(blocks_0123.xz, blocks_4567.xz) & 0x0000FFFFu) |
        (uint4(blocks_0123.yw, blocks_4567.yw) << 16);
    // Bottom-left and bottom-right host pixels - same, but for bits 16:31
    // instead of 0:15.
    xe_texture_load_dest[block_offset_host + elements_pitch_host] =
        (uint4(blocks_0123.xz, blocks_4567.xz) >> 16) |
        (uint4(blocks_0123.yw, blocks_4567.yw) & 0xFFFF0000u);
  }
}
