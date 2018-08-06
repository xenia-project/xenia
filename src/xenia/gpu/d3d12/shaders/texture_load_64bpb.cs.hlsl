#include "texture_copy.hlsli"

[numthreads(8, 32, 1)]
void main(uint3 xe_thread_id : SV_DispatchThreadID) {
  // 1 thread = 4 uint2 blocks.
  uint3 block_index = xe_thread_id;
  block_index.x <<= 2u;
  [branch] if (any(block_index >= xe_texture_copy_size)) {
    return;
  }
  uint3 block_index_guest = block_index + xe_texture_copy_guest_mip_offset;
  uint4 block_offsets_guest;
  [branch] if (xe_texture_copy_guest_pitch == XeTextureCopyGuestPitchTiled) {
    [branch] if (xe_texture_copy_is_3d) {
      block_offsets_guest = XeTextureTiledOffset3D(
          block_index_guest, xe_texture_copy_size.xy, 3u);
    } else {
      block_offsets_guest = XeTextureTiledOffset2D(
          block_index_guest.xy, xe_texture_copy_size.x, 3u);
    }
  } else {
    block_offsets_guest = uint4(0u, 8u, 16u, 24u) + XeTextureGuestLinearOffset(
        block_index_guest, xe_texture_copy_size.y, xe_texture_copy_guest_pitch,
        8u);
  }
  block_offsets_guest += xe_texture_copy_guest_base;
  uint4 blocks_01 = uint4(xe_texture_copy_source.Load2(block_offsets_guest.x),
                          xe_texture_copy_source.Load2(block_offsets_guest.y));
  uint4 blocks_23 = uint4(xe_texture_copy_source.Load2(block_offsets_guest.z),
                          xe_texture_copy_source.Load2(block_offsets_guest.w));
  blocks_01 = XeByteSwap(blocks_01, xe_texture_copy_endianness);
  blocks_23 = XeByteSwap(blocks_23, xe_texture_copy_endianness);
  uint block_offset_host = XeTextureHostLinearOffset(
      block_index, xe_texture_copy_size.y, xe_texture_copy_host_pitch, 8u) +
      xe_texture_copy_host_base;
  xe_texture_copy_dest.Store4(block_offset_host, blocks_01);
  xe_texture_copy_dest.Store4(block_offset_host + 16u, blocks_23);
}
