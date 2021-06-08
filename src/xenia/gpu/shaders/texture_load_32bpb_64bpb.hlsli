#include "texture_load.hlsli"

Buffer<uint4> xe_texture_load_source : register(t0);
RWBuffer<uint4> xe_texture_load_dest : register(u0);

[numthreads(4, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 8 packed 32-bit texels with the externally provided uint4 -> 2x
  // uint4 function (XE_TEXTURE_LOAD_32BPB_TO_64BPB) for converting to 64bpb -
  // useful for expansion of hendeca (10:11:11 or 11:11:10) to unorm16/snorm16.
  uint3 block_index = xe_thread_id << uint3(3, 0, 0);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int block_offset_host =
      (XeTextureHostLinearOffset(int3(block_index), xe_texture_load_host_pitch,
                                 xe_texture_load_size_blocks.y, 8u) +
       xe_texture_load_host_offset) >> 4;
  int block_offset_guest =
      XeTextureLoadGuestBlockOffset(int3(block_index), 4u, 2u) >> 4;
  uint endian = XeTextureLoadEndian32();
  XE_TEXTURE_LOAD_32BPB_TO_64BPB(
      XeEndianSwap32(xe_texture_load_source[block_offset_guest], endian),
      xe_texture_load_dest[block_offset_host],
      xe_texture_load_dest[block_offset_host + 1]);
  block_offset_host += 2;
  // Odd 4 blocks = even 4 blocks + 32 bytes when tiled.
  block_offset_guest += XeTextureLoadIsTiled() ? 2 : 1;
  XE_TEXTURE_LOAD_32BPB_TO_64BPB(
      XeEndianSwap32(xe_texture_load_source[block_offset_guest], endian),
      xe_texture_load_dest[block_offset_host],
      xe_texture_load_dest[block_offset_host + 1]);
}
