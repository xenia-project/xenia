#ifndef XENIA_GPU_D3D12_SHADERS_TEXTURE_COPY_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_TEXTURE_COPY_HLSLI_

#include "endian.hlsli"
#include "texture_address.hlsli"

cbuffer XeTextureLoadConstants : register(b0) {
  uint xe_texture_load_is_tiled_3d_endian;
  // Base offset in bytes.
  uint xe_texture_load_guest_offset;
  // For tiled textures - row pitch in blocks, aligned to 32.
  // For linear textures - row pitch in bytes.
  uint xe_texture_load_guest_pitch_aligned;
  // Must be aligned to 32.
  uint xe_texture_load_guest_z_stride_block_rows_aligned;

  // If this is a packed mip tail, this is aligned to tile dimensions.
  uint3 xe_texture_load_size_blocks;
  uint xe_texture_load_host_offset;

  // Base offset in bytes.
  uint xe_texture_load_host_pitch;
  uint xe_texture_load_height_texels;
};

bool XeTextureLoadIsTiled() {
  return (xe_texture_load_is_tiled_3d_endian & 1u) != 0u;
}

bool XeTextureLoadIs3D() {
  return (xe_texture_load_is_tiled_3d_endian & (1u << 1u)) != 0u;
}

uint XeTextureLoadEndian32() {
  return xe_texture_load_is_tiled_3d_endian >> 2u;
}

// bpb and bpb_log2 are separate because bpb may be not a power of 2 (like 96).
uint XeTextureLoadGuestBlockOffset(int3 block_index, uint bpb, uint bpb_log2) {
  int block_offset_guest;
  [branch] if (XeTextureLoadIsTiled()) {
    [branch] if (XeTextureLoadIs3D()) {
      block_offset_guest = XeTextureTiledOffset3D(
          block_index, xe_texture_load_guest_pitch_aligned,
          xe_texture_load_guest_z_stride_block_rows_aligned, bpb_log2);
    } else {
      block_offset_guest = XeTextureTiledOffset2D(
          block_index.xy, xe_texture_load_guest_pitch_aligned, bpb_log2);
    }
  } else {
    block_offset_guest = XeTextureGuestLinearOffset(
        block_index, xe_texture_load_guest_pitch_aligned,
        xe_texture_load_guest_z_stride_block_rows_aligned, bpb);
  }
  return uint(int(xe_texture_load_guest_offset) + block_offset_guest);
}

#endif  // XENIA_GPU_D3D12_SHADERS_TEXTURE_COPY_HLSLI_
