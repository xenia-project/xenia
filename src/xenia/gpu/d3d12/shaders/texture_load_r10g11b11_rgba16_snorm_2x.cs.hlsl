#include "pixel_formats.hlsli"
#include "texture_load.hlsli"

[numthreads(16, 16, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 1 uint R10G11B11_SNORM guest texel to RGBA16.
  uint3 block_index = xe_thread_id;
  block_index.x <<= 2u;
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  uint4 blocks = xe_texture_load_source.Load4(
      XeTextureLoadGuestBlockOffsets(xe_thread_id, 4u, 2u).x << 2u);
  blocks = XeByteSwap(blocks, xe_texture_load_endianness);
  uint block_offset_host = XeTextureHostLinearOffset(
      xe_thread_id << uint3(1u, 1u, 0u), xe_texture_load_size_blocks.y << 1u,
      xe_texture_load_host_pitch, 8u) + xe_texture_load_host_base;

  // Expand two's complement.
  uint4 blocks_host_rg =
      XeSNorm10To16(blocks & 1023u) | XeSNorm11To16((blocks >> 10u) & 2047u);
  uint4 blocks_host_ba = XeSNorm11To16(blocks >> 21u) | 0x7FFF0000u;

  // Store the texels.
  xe_texture_load_dest.Store4(block_offset_host,
                              uint4(blocks_host_rg.xy, blocks_host_ba.xy).xzyw);
  xe_texture_load_dest.Store4(block_offset_host + xe_texture_load_host_pitch,
                              uint4(blocks_host_rg.zw, blocks_host_ba.zw).xzyw);
}
