#ifndef XENIA_GPU_D3D12_SHADERS_TEXTURE_COPY_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_TEXTURE_COPY_HLSLI_

#include "byte_swap.hlsli"
#include "texture_address.hlsli"

cbuffer XeTextureLoadConstants : register(b0) {
  // Base offset in bytes.
  uint xe_texture_load_guest_base;
  // For linear textures - row byte pitch.
  uint xe_texture_load_guest_pitch;
  // In blocks - and for mipmaps, it's also power-of-two-aligned.
  uint2 xe_texture_load_guest_storage_width_height;

  uint3 xe_texture_load_size_blocks;
  uint xe_texture_load_is_3d_endian;

  // Base offset in bytes.
  uint xe_texture_load_host_base;
  uint xe_texture_load_host_pitch;
  uint xe_texture_load_height_texels;
};

#define XeTextureLoadGuestPitchTiled 0xFFFFFFFFu
bool XeTextureLoadIsTiled() {
  return xe_texture_load_guest_pitch == XeTextureLoadGuestPitchTiled;
}

bool XeTextureLoadIs3D() {
  return (xe_texture_load_is_3d_endian & 1u) != 0u;
}

uint XeTextureLoadEndian() {
  return xe_texture_load_is_3d_endian >> 1;
}

// bpb and bpb_log2 are separate because bpb may be not a power of 2 (like 96).
uint XeTextureLoadGuestBlockOffset(int3 block_index, uint bpb, uint bpb_log2) {
  int block_offset_guest;
  [branch] if (XeTextureLoadIsTiled()) {
    [branch] if (XeTextureLoadIs3D()) {
      block_offset_guest = XeTextureTiledOffset3D(
          block_index, xe_texture_load_guest_storage_width_height, bpb_log2);
    } else {
      block_offset_guest = XeTextureTiledOffset2D(
          block_index.xy, xe_texture_load_guest_storage_width_height.x,
          bpb_log2);
    }
  } else {
    block_offset_guest = XeTextureGuestLinearOffset(
        block_index, xe_texture_load_size_blocks.y, xe_texture_load_guest_pitch,
        bpb);
  }
  return uint(int(xe_texture_load_guest_base) + block_offset_guest);
}

// bpb and bpb_log2 are separate because bpb may be not a power of 2 (like 96).
uint4 XeTextureLoadGuestBlockOffsets(uint3 block_index, uint bpb,
                                     uint bpb_log2) {
  uint4 block_offsets_guest;
  [branch] if (XeTextureLoadIsTiled()) {
    [branch] if (XeTextureLoadIs3D()) {
      block_offsets_guest = XeTextureTiledOffset3D(
          block_index, xe_texture_load_guest_storage_width_height, bpb_log2);
    } else {
      block_offsets_guest = XeTextureTiledOffset2D(
          block_index.xy, xe_texture_load_guest_storage_width_height.x,
          bpb_log2);
    }
  } else {
    block_offsets_guest =
        uint4(0u, 1u, 2u, 3u) * bpb + XeTextureGuestLinearOffset(
            block_index, xe_texture_load_size_blocks.y,
            xe_texture_load_guest_pitch, bpb);
  }
  return block_offsets_guest + xe_texture_load_guest_base;
}

#endif  // XENIA_GPU_D3D12_SHADERS_TEXTURE_COPY_HLSLI_
