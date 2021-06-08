#include "texture_load.hlsli"

Buffer<uint2> xe_texture_load_source : register(t0);
RWBuffer<uint2> xe_texture_load_dest : register(u0);

[numthreads(2, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 16 blocks passed through an externally provided
  // uint2 transformation function (XE_TEXTURE_LOAD_16BPB_TRANSFORM).
  uint3 block_index = xe_thread_id << uint3(4, 0, 0);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int block_offset_host =
      (XeTextureHostLinearOffset(int3(block_index) * int3(3, 3, 1),
                                 xe_texture_load_host_pitch,
                                 xe_texture_load_size_blocks.y * 3, 2u) +
       xe_texture_load_host_offset) >> 3;
  int elements_pitch_host = xe_texture_load_host_pitch >> 3;
  int block_offset_guest =
      (XeTextureLoadGuestBlockOffset(int3(block_index), 2u, 1u) * 9) >> 3;
  uint endian = XeTextureLoadEndian32();
  int i;
  [unroll] for (i = 0; i < 16; i += 4) {
    if (i == 8 && XeTextureLoadIsTiled()) {
      // Odd 8 blocks start = even 8 blocks end + 48 guest bytes when tiled.
      block_offset_guest += (48 * 9) >> 3;
    }
    // For guest pixels 0...3 and host pixels a...i within them:
    // 0a0b0c0d0e0f0g0h0i1a1b1c1d1e1f1g1h1i2a2b2c2d2e2f2g2h2i3a3b3c3d3e3f3g3h3i
    // to:
    // 0a0b0c1a 1b1c2a2b 2c3a3b3c
    // 0d0e0f1d 1e1f2d2e 2f3d3e3f
    // 0g0h0i1g 1h1i2g2h 2i3g3h3i
    uint2 blocks_0a0b_0c0d = XE_TEXTURE_LOAD_16BPB_TRANSFORM(
        XeEndianSwap16(xe_texture_load_source[block_offset_guest++], endian));
    uint2 blocks_0e0f_0g0h = XE_TEXTURE_LOAD_16BPB_TRANSFORM(
        XeEndianSwap16(xe_texture_load_source[block_offset_guest++], endian));
    uint2 blocks_0i1a_1b1c = XE_TEXTURE_LOAD_16BPB_TRANSFORM(
        XeEndianSwap16(xe_texture_load_source[block_offset_guest++], endian));
    uint2 blocks_1d1e_1f1g = XE_TEXTURE_LOAD_16BPB_TRANSFORM(
        XeEndianSwap16(xe_texture_load_source[block_offset_guest++], endian));
    uint2 blocks_1h1i_2a2b = XE_TEXTURE_LOAD_16BPB_TRANSFORM(
        XeEndianSwap16(xe_texture_load_source[block_offset_guest++], endian));
    uint2 blocks_2c2d_2e2f = XE_TEXTURE_LOAD_16BPB_TRANSFORM(
        XeEndianSwap16(xe_texture_load_source[block_offset_guest++], endian));
    uint2 blocks_2g2h_2i3a = XE_TEXTURE_LOAD_16BPB_TRANSFORM(
        XeEndianSwap16(xe_texture_load_source[block_offset_guest++], endian));
    uint2 blocks_3b3c_3d3e = XE_TEXTURE_LOAD_16BPB_TRANSFORM(
        XeEndianSwap16(xe_texture_load_source[block_offset_guest++], endian));
    uint2 blocks_3f3g_3h3i = XE_TEXTURE_LOAD_16BPB_TRANSFORM(
        XeEndianSwap16(xe_texture_load_source[block_offset_guest++], endian));
    // R0+0 = 0a0b, 0c1a
    int block_row_offset_host = block_offset_host;
    xe_texture_load_dest[block_row_offset_host] = uint2(
        blocks_0a0b_0c0d.x,
        (blocks_0a0b_0c0d.y & 0xFFFFu) | (blocks_0i1a_1b1c.x & ~0xFFFFu));
    // R0+1 = 1b1c, 2a2b
    xe_texture_load_dest[block_row_offset_host + 1] = uint2(
        blocks_0i1a_1b1c.y,
        blocks_1h1i_2a2b.y);
    // R0+2 = 2c3a, 3b3c
    xe_texture_load_dest[block_row_offset_host + 2] = uint2(
        (blocks_2c2d_2e2f.x & 0xFFFFu) | (blocks_2g2h_2i3a.y & ~0xFFFFu),
        blocks_3b3c_3d3e.x);
    // R1+0 = 0d0e, 0f1d
    block_row_offset_host += elements_pitch_host;
    xe_texture_load_dest[block_row_offset_host] = uint2(
        (blocks_0a0b_0c0d.y >> 16u) | (blocks_0e0f_0g0h.x << 16u),
        (blocks_0e0f_0g0h.x >> 16u) | (blocks_1d1e_1f1g.x << 16u));
    // R1+1 = 1e1f, 2d2e
    xe_texture_load_dest[block_row_offset_host + 1] = uint2(
        (blocks_1d1e_1f1g.x >> 16u) | (blocks_1d1e_1f1g.y << 16u),
        (blocks_2c2d_2e2f.x >> 16u) | (blocks_2c2d_2e2f.y << 16u));
    // R1+2 = 2f3d, 3e3f
    xe_texture_load_dest[block_row_offset_host + 2] = uint2(
        (blocks_2c2d_2e2f.y >> 16u) | (blocks_3b3c_3d3e.y << 16u),
        (blocks_3b3c_3d3e.y >> 16u) | (blocks_3f3g_3h3i.x << 16u));
    // R2+0 = 0g0h, 0i1g
    block_row_offset_host += elements_pitch_host;
    xe_texture_load_dest[block_row_offset_host] = uint2(
        blocks_0e0f_0g0h.y,
        (blocks_0i1a_1b1c.x & 0xFFFFu) | (blocks_1d1e_1f1g.y & ~0xFFFFu));
    // R2+1 = 1h1i, 2g2h
    xe_texture_load_dest[block_row_offset_host + 1] = uint2(
        blocks_1h1i_2a2b.x,
        blocks_2g2h_2i3a.x);
    // R2+2 = 2i3g, 3h3i
    xe_texture_load_dest[block_row_offset_host + 2] = uint2(
        (blocks_2g2h_2i3a.y & 0xFFFFu) | (blocks_3f3g_3h3i.x & ~0xFFFFu),
        blocks_3f3g_3h3i.y);
    block_offset_host += 3;
  }
}
