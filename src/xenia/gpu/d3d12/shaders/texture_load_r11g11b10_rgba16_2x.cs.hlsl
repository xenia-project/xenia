#include "texture_load.hlsli"

[numthreads(16, 16, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 1 uint R11G11B10_UNORM guest texel to RGBA16.
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

  // Upper 2 host texels.
  // Red and blue.
  uint4 blocks_host =
      (((blocks.xxyy >> uint2(0u, 22u).xyxy) & uint2(2047u, 1023u).xyxy) <<
           uint2(5u, 6u).xyxy) |
      ((blocks.xxyy >> uint2(6u, 26u).xyxy) & uint2(31u, 63u).xyxy);
  // Green.
  blocks_host.xz |= ((blocks.xy & (2047u << 11u)) << (21u - 11u)) |
                    ((blocks.xy & (31u << 17u)) >> (17u - 16u));
  // Alpha.
  blocks_host.yw |= 0xFFFF0000u;
  xe_texture_load_dest.Store4(block_offset_host, blocks_host);

  // Lower 2 host texels.
  // Red and blue.
  blocks_host =
      (((blocks.zzww >> uint2(0u, 22u).xyxy) & uint2(2047u, 1023u).xyxy) <<
           uint2(5u, 6u).xyxy) |
      ((blocks.zzww >> uint2(6u, 26u).xyxy) & uint2(31u, 63u).xyxy);
  // Green.
  blocks_host.xz |= ((blocks.zw & (2047u << 11u)) << (21u - 11u)) |
                    ((blocks.zw & (31u << 17u)) >> (17u - 16u));
  // Alpha.
  blocks_host.yw |= 0xFFFF0000u;
  xe_texture_load_dest.Store4(block_offset_host + xe_texture_load_host_pitch,
                              blocks_host);
}
