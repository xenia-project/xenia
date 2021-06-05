#include "texture_load.hlsli"

Buffer<uint4> xe_texture_load_source : register(t0);
RWBuffer<uint4> xe_texture_load_dest : register(u0);

[numthreads(2, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 16 blocks passed through an externally provided
  // uint4 transformation function (XE_TEXTURE_LOAD_16BPB_TRANSFORM).
  uint3 block_index = xe_thread_id << uint3(4, 0, 0);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int block_offset_host =
      (XeTextureHostLinearOffset(int3(block_index) << int3(1, 1, 0),
                                 xe_texture_load_host_pitch,
                                 xe_texture_load_size_blocks.y << 1, 2u) +
       xe_texture_load_host_offset) >> 4;
  int elements_pitch_host = xe_texture_load_host_pitch >> 4;
  int block_offset_guest =
      XeTextureLoadGuestBlockOffset(int3(block_index), 2u, 1u) >> (4 - 2);
  uint endian = XeTextureLoadEndian32();
  int i;
  [unroll] for (i = 0; i < 16; i += 4) {
    if (i == 8 && XeTextureLoadIsTiled()) {
      // Odd 8 blocks start = even 8 blocks end + 48 guest bytes when tiled.
      block_offset_guest += 3 << 2;
    }
    // X = 0:15 - top-left of 0, 16:31 - top-right of 0.
    // Y = 0:15 - bottom-left of 0, 16:31 - bottom-right of 0.
    // Z = 0:15 - top-left of 1, 16:31 - top-right of 1.
    // W = 0:15 - bottom-left of 1, 16:31 - bottom-right of 1.
    uint4 blocks_01 =
        XeEndianSwap16(xe_texture_load_source[block_offset_guest++], endian);
    // X = 0:15 - top-left of 2, 16:31 - top-right of 2.
    // Y = 0:15 - bottom-left of 2, 16:31 - bottom-right of 2.
    // Z = 0:15 - top-left of 3, 16:31 - top-right of 3.
    // W = 0:15 - bottom-left of 3, 16:31 - bottom-right of 3.
    uint4 blocks_23 =
        XeEndianSwap16(xe_texture_load_source[block_offset_guest++], endian);
    xe_texture_load_dest[block_offset_host] =
        XE_TEXTURE_LOAD_16BPB_TRANSFORM(uint4(blocks_01.xz, blocks_23.xz));
    xe_texture_load_dest[block_offset_host + elements_pitch_host] =
        XE_TEXTURE_LOAD_16BPB_TRANSFORM(uint4(blocks_01.yw, blocks_23.yw));
    ++block_offset_host;
  }
}
