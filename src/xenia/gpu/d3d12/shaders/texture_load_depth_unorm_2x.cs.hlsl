#include "pixel_formats.hlsli"
#include "texture_load.hlsli"

[numthreads(16, 16, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 1 guest depth texel (24-bit unorm depth converted to 32-bit,
  // can't read stencil in shaders anyway because it would require a separate
  // DXGI_FORMAT_X24_TYPELESS_G8_UINT SRV).
  [branch] if (any(xe_thread_id >= xe_texture_load_size_blocks)) {
    return;
  }
  uint4 blocks = xe_texture_load_source.Load4(
      XeTextureLoadGuestBlockOffsets(xe_thread_id, 4u, 2u).x << 2u);
  blocks = asuint(float4(XeByteSwap(blocks, xe_texture_load_endianness) >> 8u) /
                  16777215.0);
  uint block_offset_host = XeTextureHostLinearOffset(
      xe_thread_id << uint3(1u, 1u, 0u), xe_texture_load_size_blocks.y << 1u,
      xe_texture_load_host_pitch, 4u) + xe_texture_load_host_base;
  xe_texture_load_dest.Store2(block_offset_host, blocks.xy);
  xe_texture_load_dest.Store2(block_offset_host + xe_texture_load_host_pitch,
                              blocks.zw);
}
