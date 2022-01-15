#include "texture_load.hlsli"

Buffer<uint4> xe_texture_load_source : register(t0);
RWBuffer<uint4> xe_texture_load_dest : register(u0);

[numthreads(4, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 8 blocks passed through an externally provided
  // uint4 transformation function (XE_TEXTURE_LOAD_32BPB_TRANSFORM).
  uint3 block_index = xe_thread_id << uint3(3u, 0u, 0u);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int block_offset_host =
      (XeTextureHostLinearOffset(int3(block_index), xe_texture_load_host_pitch,
                                 xe_texture_load_size_blocks.y, 4u) +
       xe_texture_load_host_offset) >> 4u;
  uint block_offset_guest =
      XeTextureLoadGuestBlockOffset(block_index, 4u, 2u) >> 4u;
  uint endian = XeTextureLoadEndian32();
  xe_texture_load_dest[block_offset_host] = XE_TEXTURE_LOAD_32BPB_TRANSFORM(
      XeEndianSwap32(xe_texture_load_source[block_offset_guest], endian));
  ++block_offset_host;
  block_offset_guest +=
      XeTextureLoadRightConsecutiveBlocksOffset(block_index.x, 2u) >> 4u;
  xe_texture_load_dest[block_offset_host] = XE_TEXTURE_LOAD_32BPB_TRANSFORM(
      XeEndianSwap32(xe_texture_load_source[block_offset_guest], endian));
}
