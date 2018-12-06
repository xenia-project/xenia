#include "texture_load.hlsli"

[numthreads(4, 16, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4x1 ubyte guest texels.
  uint3 block_index = xe_thread_id;
  block_index.x <<= 2u;
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  uint4 block_offsets_guest =
      XeTextureLoadGuestBlockOffsets(block_index, 1u, 0u) << 2u;
  uint4 blocks = uint4(xe_texture_load_source.Load(block_offsets_guest.x),
                       xe_texture_load_source.Load(block_offsets_guest.y),
                       xe_texture_load_source.Load(block_offsets_guest.z),
                       xe_texture_load_source.Load(block_offsets_guest.w));
  uint block_offset_host = XeTextureHostLinearOffset(
      xe_thread_id << uint3(3u, 1u, 0u), xe_texture_load_size_blocks.y << 1u,
      xe_texture_load_host_pitch, 1u) + xe_texture_load_host_base;
  xe_texture_load_dest.Store2(block_offset_host,
                              (blocks.xz & 0x0000FFFFu) | (blocks.yw << 16u));
  xe_texture_load_dest.Store2(block_offset_host + xe_texture_load_host_pitch,
                              (blocks.xz >> 16u) | (blocks.yw & 0xFFFF0000u));
}
