#include "texture_load.hlsli"

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 ushort blocks.
  uint3 block_index = xe_thread_id;
  block_index.x <<= 2u;
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  uint4 block_offsets_guest =
      XeTextureLoadGuestBlockOffsets(block_index, 2u, 1u);
  uint4 dword_offsets_guest = block_offsets_guest & ~3u;
  uint4 blocks = uint4(xe_texture_load_source.Load(dword_offsets_guest.x),
                       xe_texture_load_source.Load(dword_offsets_guest.y),
                       xe_texture_load_source.Load(dword_offsets_guest.z),
                       xe_texture_load_source.Load(dword_offsets_guest.w));
  blocks = (blocks >> ((block_offsets_guest & 2u) << 3u)) & 0xFFFFu;
  blocks = XeByteSwap16(blocks, xe_texture_load_endianness);

  // Swap components - Xenos 16-bit packed textures are RGBA, but in Direct3D 12
  // they are BGRA.
  if (xe_texture_load_guest_format == 3u) {
    // k_1_5_5_5.
    blocks = (blocks & ((31u << 5u) | (1u << 15u))) | ((blocks & 31u) << 10u) |
             (blocks >> 10u);
  } else if (xe_texture_load_guest_format == 4u) {
    // k_5_6_5.
    blocks = (blocks & (63u << 5u)) | ((blocks & 31u) << 11u) | (blocks >> 11u);
  } else if (xe_texture_load_guest_format == 5u) {
    // k_6_5_5 - RRRRR GGGGG BBBBBB to GGGGG BBBBBB RRRRR (use RBGA swizzle when
    // reading).
    blocks = ((blocks & 31u) << 11u) | (blocks >> 5u);
  } else if (xe_texture_load_guest_format == 15u) {
    // k_4_4_4_4.
    blocks =
        (blocks & 0xF0F0u) | ((blocks & 15u) << 8u) | ((blocks >> 8u) & 15u);
  }

  uint block_offset_host = XeTextureHostLinearOffset(
      block_index, xe_texture_load_size_blocks.y, xe_texture_load_host_pitch,
      2u) + xe_texture_load_host_base;
  xe_texture_load_dest.Store2(block_offset_host,
                              blocks.xz | (blocks.yw << 16u));
}
