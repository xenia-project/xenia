#include "texture_load.hlsli"

Buffer<uint2> xe_texture_load_source : register(t0);
RWBuffer<uint2> xe_texture_load_dest : register(u0);

[numthreads(2, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 16 blocks.
  uint3 block_index = xe_thread_id << uint3(4, 0, 0);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int block_offset_host =
      (XeTextureHostLinearOffset(int3(block_index) * int3(3, 3, 1),
                                 xe_texture_load_host_pitch,
                                 xe_texture_load_size_blocks.y * 3, 1u) +
       xe_texture_load_host_offset) >> 3;
  int elements_pitch_host = xe_texture_load_host_pitch >> 3;
  int block_offset_guest =
      (XeTextureLoadGuestBlockOffset(int3(block_index), 1u, 0u) * 9) >> 3;
  int i;
  [unroll] for (i = 0; i < 2; ++i) {
    if (i) {
      block_offset_host += 3;
      // Odd 8 blocks start = even 8 blocks end + 56 guest bytes when tiled.
      if (XeTextureLoadIsTiled()) {
        block_offset_guest += (56 * 9) >> 3;
      }
    }
    // For guest pixels 0...3 and host pixels a...i within them:
    // 0a0b0c0d0e0f0g0h0i1a1b1c1d1e1f1g1h1i2a2b2c2d2e2f2g2h2i3a3b3c3d3e3f3g3h3i-
    // 4a4b4c4d4e4f4g4h4i5a5b5c5d5e5f5g5h5i6a6b6c6d6e6f6g6h6i7a7b7c7d7e7f7g7h7i
    // to:
    // 0a0b0c1a 1b1c2a2b 2c3a3b3c 4a4b4c5a 5b5c6a6b 6c7a7b7c
    // 0d0e0f1d 1e1f2d2e 2f3d3e3f 4d4e4f5d 5e5f6d6e 6f7d7e7f
    // 0g0h0i1g 1h1i2g2h 2i3g3h3i 4g4h4i5g 5h5i6g6h 6i7g7h7i
    uint2 blocks_0a0b0c0d_0e0f0g0h =
        xe_texture_load_source[block_offset_guest++];
    uint2 blocks_0i1a1b1c_1d1e1f1g =
        xe_texture_load_source[block_offset_guest++];
    uint2 blocks_1h1i2a2b_2c2d2e2f =
        xe_texture_load_source[block_offset_guest++];
    uint2 blocks_2g2h2i3a_3b3c3d3e =
        xe_texture_load_source[block_offset_guest++];
    uint2 blocks_3f3g3h3i_4a4b4c4d =
        xe_texture_load_source[block_offset_guest++];
    uint2 blocks_4e4f4g4h_4i5a5b5c =
        xe_texture_load_source[block_offset_guest++];
    uint2 blocks_5d5e5f5g_5h5i6a6b =
        xe_texture_load_source[block_offset_guest++];
    uint2 blocks_6c6d6e6f_6g6h6i7a =
        xe_texture_load_source[block_offset_guest++];
    uint2 blocks_7b7c7d7e_7f7g7h7i =
        xe_texture_load_source[block_offset_guest++];
    // R0+0 = 0a0b0c1a, 1b1c2a2b
    int block_row_offset_host = block_offset_host;
    xe_texture_load_dest[block_row_offset_host] = uint2(
        (blocks_0a0b0c0d_0e0f0g0h.x & 0x00FFFFFFu) |
            ((blocks_0i1a1b1c_1d1e1f1g.x >> 8u) << 24u),
        (blocks_0i1a1b1c_1d1e1f1g.x >> 16u) |
            (blocks_1h1i2a2b_2c2d2e2f.x & 0xFFFF0000u));
    // R0+1 = 2c3a3b3c, 4a4b4c5a
    xe_texture_load_dest[block_row_offset_host + 1] = uint2(
        (blocks_1h1i2a2b_2c2d2e2f.y & 0x000000FFu) |
            ((blocks_2g2h2i3a_3b3c3d3e.x >> 24u) << 8u) |
            (blocks_2g2h2i3a_3b3c3d3e.y << 16u),
        (blocks_3f3g3h3i_4a4b4c4d.y & 0x00FFFFFFu) |
            ((blocks_4e4f4g4h_4i5a5b5c.y >> 8u) << 24u));
    // R0+2 = 5b5c6a6b, 6c7a7b7c
    xe_texture_load_dest[block_row_offset_host + 2] = uint2(
        (blocks_4e4f4g4h_4i5a5b5c.y >> 16u) |
            (blocks_5d5e5f5g_5h5i6a6b.y & 0xFFFF0000u),
        (blocks_6c6d6e6f_6g6h6i7a.x & 0x000000FFu) |
            ((blocks_6c6d6e6f_6g6h6i7a.y >> 24u) << 8u) |
            (blocks_7b7c7d7e_7f7g7h7i.x << 16u));
    // R1+0 = 0d0e0f1d, 1e1f2d2e
    block_row_offset_host += elements_pitch_host;
    xe_texture_load_dest[block_row_offset_host] = uint2(
        (blocks_0a0b0c0d_0e0f0g0h.x >> 24u) |
            ((blocks_0a0b0c0d_0e0f0g0h.y << 16u) >> 8u) |
            (blocks_0i1a1b1c_1d1e1f1g.y << 24u),
        ((blocks_0i1a1b1c_1d1e1f1g.y << 8u) >> 16u) |
            ((blocks_1h1i2a2b_2c2d2e2f.y >> 8u) << 16u));
    // R1+1 = 2f3d3e3f, 4d4e4f5d
    xe_texture_load_dest[block_row_offset_host + 1] = uint2(
        (blocks_1h1i2a2b_2c2d2e2f.y >> 24u) |
            ((blocks_2g2h2i3a_3b3c3d3e.y >> 16u) << 8u) |
            (blocks_3f3g3h3i_4a4b4c4d.x << 24u),
        (blocks_3f3g3h3i_4a4b4c4d.y >> 24u) |
            ((blocks_4e4f4g4h_4i5a5b5c.x << 16u) >> 8u) |
            (blocks_5d5e5f5g_5h5i6a6b.x << 24u));
    // R1+2 = 5e5f6d6e, 6f7d7e7f
    xe_texture_load_dest[block_row_offset_host + 2] = uint2(
        ((blocks_5d5e5f5g_5h5i6a6b.x << 8u) >> 16u) |
            ((blocks_6c6d6e6f_6g6h6i7a.x >> 8u) << 16u),
        (blocks_6c6d6e6f_6g6h6i7a.x >> 24u) |
            ((blocks_7b7c7d7e_7f7g7h7i.x >> 16u) << 8u) |
            (blocks_7b7c7d7e_7f7g7h7i.x << 24u));
    // R2+0 = 0g0h0i1g, 1h1i2g2h
    block_row_offset_host += elements_pitch_host;
    xe_texture_load_dest[block_row_offset_host] = uint2(
        (blocks_0a0b0c0d_0e0f0g0h.y >> 16u) |
            ((blocks_0i1a1b1c_1d1e1f1g.x << 24u) >> 8u) |
            (blocks_0i1a1b1c_1d1e1f1g.y & 0xFF000000u),
        (blocks_1h1i2a2b_2c2d2e2f.x & 0x0000FFFFu) |
            (blocks_2g2h2i3a_3b3c3d3e.x << 16u));
    // R2+1 = 2i3g3h3i, 4g4h4i5g
    xe_texture_load_dest[block_row_offset_host + 1] = uint2(
        ((blocks_2g2h2i3a_3b3c3d3e.x << 8u) >> 24u) |
            (blocks_3f3g3h3i_4a4b4c4d.x & 0xFFFFFF00u),
        (blocks_4e4f4g4h_4i5a5b5c.x >> 16u) |
            ((blocks_4e4f4g4h_4i5a5b5c.y << 24u) >> 8u) |
            (blocks_5d5e5f5g_5h5i6a6b.x & 0xFF000000u));
    // R2+2 = 5h5i6g6h, 6i7g7h7i
    xe_texture_load_dest[block_row_offset_host + 2] = uint2(
        (blocks_5d5e5f5g_5h5i6a6b.y & 0x0000FFFFu) |
            (blocks_6c6d6e6f_6g6h6i7a.y << 16u),
        ((blocks_6c6d6e6f_6g6h6i7a.y << 8u) >> 24u) |
            (blocks_7b7c7d7e_7f7g7h7i.y & 0xFFFFFF00u));
  }
}
