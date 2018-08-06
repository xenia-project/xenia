#ifndef XENIA_GPU_D3D12_SHADERS_TEXTURE_COPY_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_TEXTURE_COPY_HLSLI_

#include "byte_swap.hlsli"
#include "texture_address.hlsli"

cbuffer xe_texture_copy_constants : register(b0) {
  uint xe_texture_copy_guest_base;
  // For linear textures - row byte pitch.
  uint xe_texture_copy_guest_pitch;
  uint xe_texture_copy_host_base;
  uint xe_texture_copy_host_pitch;

  // Size in blocks.
  uint3 xe_texture_copy_size;
  bool xe_texture_copy_is_3d;

  // Offset within the packed mip for small mips.
  uint3 xe_texture_copy_guest_mip_offset;
  uint xe_texture_copy_endianness;
};

#define XeTextureCopyGuestPitchTiled 0xFFFFFFFFu

ByteAddressBuffer xe_texture_copy_source : register(t0);
RWByteAddressBuffer xe_texture_copy_dest : register(u0);

#endif  // XENIA_GPU_D3D12_SHADERS_TEXTURE_COPY_HLSLI_
