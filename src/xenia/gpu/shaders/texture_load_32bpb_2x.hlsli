#include "texture_load.hlsli"

Buffer<uint4> xe_texture_load_source : register(t0);
RWBuffer<uint4> xe_texture_load_dest : register(u0);

[numthreads(4, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 8 blocks passed through an externally provided
  // uint4 transformation function (XE_TEXTURE_LOAD_32BPB_TRANSFORM).
  uint3 block_index = xe_thread_id << uint3(3, 0, 0);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int block_offset_host =
      (XeTextureHostLinearOffset(int3(block_index) << int3(1, 1, 0),
                                 xe_texture_load_host_pitch,
                                 xe_texture_load_size_blocks.y << 1, 4u) +
       xe_texture_load_host_offset) >> 4;
  int elements_pitch_host = xe_texture_load_host_pitch >> 4;
  int block_offset_guest =
      XeTextureLoadGuestBlockOffset(int3(block_index), 4u, 2u) >> (4 - 2);
  uint endian = XeTextureLoadEndian32();
  int i;
  [unroll] for (i = 0; i < 8; i += 2) {
    if (i == 4 && XeTextureLoadIsTiled()) {
      // Odd 4 blocks start = even 4 blocks end + 16 guest bytes when tiled.
      block_offset_guest += 1 << 2;
    }
    // TTBB TTBB -> TTTT on the top row, BBBB on the bottom row.
    uint4 block_0 = XeEndianSwap32(xe_texture_load_source[block_offset_guest++],
                                   endian);
    uint4 block_1 = XeEndianSwap32(xe_texture_load_source[block_offset_guest++],
                                   endian);
    xe_texture_load_dest[block_offset_host] =
        XE_TEXTURE_LOAD_32BPB_TRANSFORM(uint4(block_0.xy, block_1.xy));
    xe_texture_load_dest[block_offset_host + elements_pitch_host] =
        XE_TEXTURE_LOAD_32BPB_TRANSFORM(uint4(block_0.zw, block_1.zw));
    ++block_offset_host;
  }
}
