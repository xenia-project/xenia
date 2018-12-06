#include "texture_load.hlsli"

[numthreads(8, 16, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 2x1 ushort guest texels.
  uint3 block_index = xe_thread_id;
  block_index.x <<= 1u;
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  uint2 block_offsets_guest =
      XeTextureLoadGuestBlockOffsets(block_index, 2u, 1u).xy << 2u;
  uint4 blocks = uint4(xe_texture_load_source.Load2(block_offsets_guest.x),
                       xe_texture_load_source.Load2(block_offsets_guest.y));
  blocks = XeByteSwap16(blocks, xe_texture_load_endianness);

  // Swap components - Xenos 16-bit packed textures are RGBA, but in Direct3D 12
  // they are BGRA.
  if (xe_texture_load_guest_format == 3u) {
    // k_1_5_5_5.
    blocks = (blocks & 0x83E083E0u) | ((blocks & 0x001F001Fu) << 10u) |
             ((blocks & 0x7C007C00u) >> 10u);
  } else if (xe_texture_load_guest_format == 4u) {
    // k_5_6_5.
    blocks = (blocks & 0x07E007E0u) | ((blocks & 0x001F001Fu) << 11u) |
             ((blocks & 0xF800F800u) >> 11u);
  } else if (xe_texture_load_guest_format == 5u) {
    // k_6_5_5 - RRRRR GGGGG BBBBBB to GGGGG BBBBBB RRRRR (use RBGA swizzle when
    // reading).
    blocks = ((blocks & 0x001F001Fu) << 11u) | ((blocks & 0xFFE0FFE0) >> 5u);
  } else if (xe_texture_load_guest_format == 15u) {
    // k_4_4_4_4.
    blocks = (blocks & 0xF0F0F0F0u) | ((blocks & 0x000F000Fu) << 8u) |
             ((blocks & 0x0F000F00u) >> 8u);
  }

  uint block_offset_host = XeTextureHostLinearOffset(
      xe_thread_id << uint3(2u, 1u, 0u), xe_texture_load_size_blocks.y << 1u,
      xe_texture_load_host_pitch, 2u) + xe_texture_load_host_base;
  xe_texture_load_dest.Store2(block_offset_host, blocks.xz);
  xe_texture_load_dest.Store2(block_offset_host + xe_texture_load_host_pitch,
                              blocks.yw);
}
