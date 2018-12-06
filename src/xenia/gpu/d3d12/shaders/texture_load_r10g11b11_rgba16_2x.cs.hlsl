#include "texture_load.hlsli"

[numthreads(16, 16, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 1 uint R10G11B11_UNORM guest texel to RGBA16.
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
      (((blocks.xxyy >> uint2(0u, 21u).xyxy) & uint2(1023u, 2047u).xyxy) <<
           uint2(6u, 5u).xyxy) |
      ((blocks.xxyy >> uint2(4u, 27u).xyxy) & uint2(63u, 31u).xyxy);
  // Green. The 5 bits to be duplicated to the bottom are already at 16.
  blocks_host.xz |= ((blocks.xy & (2047u << 10u)) << (21u - 10u)) |
                    (blocks.xy & (31u << 16u));
  // Alpha.
  blocks_host.yw |= 0xFFFF0000u;
  xe_texture_load_dest.Store4(block_offset_host, blocks_host);

  // Lower 2 host texels.
  // Red and blue.
  blocks_host =
      (((blocks.zzww >> uint2(0u, 21u).xyxy) & uint2(1023u, 2047u).xyxy) <<
           uint2(6u, 5u).xyxy) |
      ((blocks.zzww >> uint2(4u, 27u).xyxy) & uint2(63u, 31u).xyxy);
  // Green. The 5 bits to be duplicated to the bottom are already at 16.
  blocks_host.xz |= ((blocks.zw & (2047u << 10u)) << (21u - 10u)) |
                    (blocks.zw & (31u << 16u));
  // Alpha.
  blocks_host.yw |= 0xFFFF0000u;
  xe_texture_load_dest.Store4(block_offset_host + xe_texture_load_host_pitch,
                              blocks_host);
}
