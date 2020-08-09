#include "texture_load.hlsli"

Buffer<uint4> xe_texture_load_source : register(t0);
RWBuffer<uint4> xe_texture_load_dest : register(u0);

[numthreads(4, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 8 depth texels, 24-bit unorm depth converted to 32-bit, can't
  // read stencil in shaders anyway because it would require a separate
  // DXGI_FORMAT_X32_TYPELESS_G8X24_UINT SRV.
  uint3 block_index = xe_thread_id << uint3(3, 0, 0);
  [branch] if (any(block_index >= xe_texture_load_size_blocks)) {
    return;
  }
  int block_offset_host =
      (XeTextureHostLinearOffset(int3(block_index),
                                 xe_texture_load_size_blocks.y,
                                 xe_texture_load_host_pitch, 4u) +
       xe_texture_load_host_base) >> 4;
  int block_offset_guest =
      XeTextureLoadGuestBlockOffset(int3(block_index), 4u, 2u) >> 4;
  uint endian = XeTextureLoadEndian32();
  xe_texture_load_dest[block_offset_host] = asuint(
      float4(XeEndianSwap32(xe_texture_load_source[block_offset_guest], endian)
                 >> 8) / 16777215.0);
  ++block_offset_host;
  // Odd 4 blocks = even 4 blocks + 32 bytes when tiled.
  block_offset_guest += XeTextureLoadIsTiled() ? 2 : 1;
  xe_texture_load_dest[block_offset_host] = asuint(
      float4(XeEndianSwap32(xe_texture_load_source[block_offset_guest], endian)
                 >> 8) / 16777215.0);
}
