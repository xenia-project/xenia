#include "pixel_formats.hlsli"
#include "texture_load.hlsli"

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 uint R11G11B10_SNORM texels to RGBA16.
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
      8u) + xe_texture_load_host_base;

  // Expand two's complement.
  uint4 blocks_host_rg =
      XeSNorm11To16(blocks & 2047u) | XeSNorm11To16((blocks >> 11u) & 2047u);
  uint4 blocks_host_ba = XeSNorm10To16(blocks >> 22u) | 0x7FFF0000u;

  // Store the texels.
  xe_texture_load_dest.Store4(block_offset_host,
                              uint4(blocks_host_rg.xy, blocks_host_ba.xy).xzyw);
  xe_texture_load_dest.Store4(block_offset_host + 16u,
                              uint4(blocks_host_rg.zw, blocks_host_ba.zw).xzyw);
}
