#ifndef XENIA_GPU_D3D12_SHADERS_TEXTURE_TILE_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_TEXTURE_TILE_HLSLI_

#include "byte_swap.hlsli"
#include "texture_address.hlsli"

cbuffer XeTextureTileConstants : register(b0) {
  // Either from the start of the shared memory or from the start of the typed
  // UAV, in bytes.
  uint xe_texture_tile_guest_base;
  // 0:2 - endianness (up to Xin128).
  // 3:31 - actual guest texture width.
  uint xe_texture_tile_endian_guest_pitch;
  // Size to copy, texels with index bigger than this won't be written.
  // Width in the lower 16 bits, height in the upper.
  uint xe_texture_tile_size;
  // Byte offset to the first texel from the beginning of the source buffer.
  uint xe_texture_tile_host_base;
  // Row pitch of the source buffer.
  uint xe_texture_tile_host_pitch;
}

ByteAddressBuffer xe_texture_tile_source : register(t0);
// The target is u0, may be a raw UAV or a typed UAV depending on the format.

#endif  // XENIA_GPU_D3D12_SHADERS_TEXTURE_TILE_HLSLI_
