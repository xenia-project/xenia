#ifndef XENIA_GPU_D3D12_SHADERS_TEXTURE_COPY_HLSLI_
#define XENIA_GPU_D3D12_SHADERS_TEXTURE_COPY_HLSLI_

#include "byte_swap.hlsli"
#include "texture_address.hlsli"

cbuffer XeTextureCopyConstants : register(b0) {
  uint xe_texture_copy_guest_base;
  // For linear textures - row byte pitch.
  uint xe_texture_copy_guest_pitch;
  uint xe_texture_copy_host_base;
  uint xe_texture_copy_host_pitch;

  uint3 xe_texture_copy_size_texels;
  bool xe_texture_copy_is_3d;

  uint3 xe_texture_copy_size_blocks;
  uint xe_texture_copy_endianness;

  // Offset within the packed mip for small mips.
  uint3 xe_texture_copy_guest_mip_offset;
};

#define XeTextureCopyGuestPitchTiled 0xFFFFFFFFu

ByteAddressBuffer xe_texture_copy_source : register(t0);
RWByteAddressBuffer xe_texture_copy_dest : register(u0);

// bpb and bpb_log2 are separate because bpb may be not a power of 2 (like 96).
uint4 XeTextureCopyGuestBlockOffsets(uint3 block_index, uint bpb,
                                     uint bpb_log2) {
  uint3 block_index_guest = block_index + xe_texture_copy_guest_mip_offset;
  uint4 block_offsets_guest;
  [branch] if (xe_texture_copy_guest_pitch == XeTextureCopyGuestPitchTiled) {
    [branch] if (xe_texture_copy_is_3d) {
      block_offsets_guest = XeTextureTiledOffset3D(
          block_index_guest, xe_texture_copy_size_blocks.xy, bpb_log2);
    } else {
      block_offsets_guest = XeTextureTiledOffset2D(
          block_index_guest.xy, xe_texture_copy_size_blocks.x, bpb_log2);
    }
  } else {
    block_offsets_guest =
        uint4(0u, 1u, 2u, 3u) * bpb + XeTextureGuestLinearOffset(
            block_index_guest, xe_texture_copy_size_blocks.y,
            xe_texture_copy_guest_pitch, bpb);
  }
  return block_offsets_guest + xe_texture_copy_guest_base;
}

#endif  // XENIA_GPU_D3D12_SHADERS_TEXTURE_COPY_HLSLI_
