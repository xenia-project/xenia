#include "texture_load.hlsli"

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 ubyte blocks.
  uint3 block_index = xe_thread_id;
  block_index.x <<= 2u;
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  uint4 block_offsets_guest =
      XeTextureLoadGuestBlockOffsets(block_index, 1u, 0u);
  uint4 dword_offsets_guest = block_offsets_guest & ~3u;
  uint4 blocks = uint4(xe_texture_load_source.Load(dword_offsets_guest.x),
                       xe_texture_load_source.Load(dword_offsets_guest.y),
                       xe_texture_load_source.Load(dword_offsets_guest.z),
                       xe_texture_load_source.Load(dword_offsets_guest.w));
  blocks = (blocks >> ((block_offsets_guest & 3u) << 3u)) & 0xFFu;
  blocks <<= uint4(0u, 8u, 16u, 24u);
  blocks.xy |= blocks.zw;
  blocks.x |= blocks.y;
  uint block_offset_host = XeTextureHostLinearOffset(
      block_index, xe_texture_load_size_blocks.y, xe_texture_load_host_pitch,
      1u) + xe_texture_load_host_base;
  xe_texture_load_dest.Store(block_offset_host, blocks.x);
}
