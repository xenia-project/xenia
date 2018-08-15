#include "texture_copy.hlsli"

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 ushort blocks.
  uint3 block_index = xe_thread_id;
  block_index.x <<= 2u;
  [branch] if (any(block_index >= xe_texture_copy_size_blocks)) {
    return;
  }
  uint4 block_offsets_guest =
      XeTextureCopyGuestBlockOffsets(block_index, 2u, 1u);
  uint4 dword_offsets_guest = block_offsets_guest & ~3u;
  uint4 blocks = uint4(xe_texture_copy_source.Load(dword_offsets_guest.x),
                       xe_texture_copy_source.Load(dword_offsets_guest.y),
                       xe_texture_copy_source.Load(dword_offsets_guest.z),
                       xe_texture_copy_source.Load(dword_offsets_guest.w));
  blocks = (blocks >> ((block_offsets_guest & 2u) << 3u)) & 0xFFFFu;
  blocks = XeByteSwap16(blocks, xe_texture_copy_endianness);
  uint block_offset_host = XeTextureHostLinearOffset(
      block_index, xe_texture_copy_size_blocks.y, xe_texture_copy_host_pitch,
      2u) + xe_texture_copy_host_base;
  xe_texture_copy_dest.Store2(block_offset_host,
                              blocks.xz | (blocks.yw << 16u));
}
