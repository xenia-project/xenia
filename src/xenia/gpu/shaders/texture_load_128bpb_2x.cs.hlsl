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
      (XeTextureHostLinearOffset(int3(block_index) << int3(1, 1, 0),
                                 xe_texture_load_host_pitch,
                                 xe_texture_load_size_blocks.y << 1, 16u) +
       xe_texture_load_host_offset) >> 4;
  int elements_pitch_host = xe_texture_load_host_pitch >> 4;
  int block_offset_guest =
      XeTextureLoadGuestBlockOffset(int3(block_index), 16u, 4u) >> (4 - 2);
  uint endian = XeTextureLoadEndian32();
  int i;
  [unroll] for (i = 0; i < 2; ++i) {
    if (i) {
      block_offset_host += 2;
      if (XeTextureLoadIsTiled()) {
        // Odd block start = even block end + 16 guest bytes when tiled.
        block_offset_guest += 1 << 2;
      }
    }
    // Top-left host texel.
    xe_texture_load_dest[block_offset_host] =
        XeEndianSwap32(xe_texture_load_source[block_offset_guest++], endian);
    // Top-right host texel.
    xe_texture_load_dest[block_offset_host + 1] =
        XeEndianSwap32(xe_texture_load_source[block_offset_guest++], endian);
    // Bottom-left host texel.
    xe_texture_load_dest[block_offset_host + elements_pitch_host] =
        XeEndianSwap32(xe_texture_load_source[block_offset_guest++], endian);
    // Bottom-right host texel.
    xe_texture_load_dest[block_offset_host + elements_pitch_host + 1] =
        XeEndianSwap32(xe_texture_load_source[block_offset_guest++], endian);
  }
}
