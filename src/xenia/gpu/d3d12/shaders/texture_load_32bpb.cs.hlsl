#include "texture_load.hlsli"

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 uint blocks.
  uint3 block_index = xe_thread_id;
  block_index.x <<= 2u;
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  uint4 block_offsets_guest =
      XeTextureLoadGuestBlockOffsets(block_index, 4u, 2u);
  uint4 blocks = uint4(xe_texture_load_source.Load(block_offsets_guest.x),
                       xe_texture_load_source.Load(block_offsets_guest.y),
                       xe_texture_load_source.Load(block_offsets_guest.z),
                       xe_texture_load_source.Load(block_offsets_guest.w));
  blocks = XeByteSwap(blocks, xe_texture_load_endianness);
  uint block_offset_host = XeTextureHostLinearOffset(
      block_index, xe_texture_load_size_blocks.y, xe_texture_load_host_pitch,
      4u) + xe_texture_load_host_base;
  xe_texture_load_dest.Store4(block_offset_host, blocks);
}
