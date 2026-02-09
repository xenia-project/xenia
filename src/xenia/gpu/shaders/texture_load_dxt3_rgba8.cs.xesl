/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "pixel_formats.xesli"
#include "texture_load.xesli"

XE_TEXTURE_LOAD_ENTRY(16)
{
  // 1 thread = 2 DXT3 blocks to 8x4 R8G8B8A8 texels.
  XeTextureLoadInfo load_info = XeTextureLoadGetInfo(pass_push_consts_xe);
  uint3_xe block_index = in_global_thread_id_xe << uint3_xe(1u, 0u, 0u);
  dont_flatten_xe
  if (any(greater_than_equal_xe(block_index.xy, load_info.size_blocks.xy))) {
    return;
  }
  uint3_xe texel_index_host = block_index << uint3_xe(2u, 2u, 0u);
  uint block_offset_host =
      load_info.host_offset +
      uint(XeTextureHostLinearOffset(int3_xe(texel_index_host),
                                     load_info.host_pitch,
                                     load_info.height_texels, 4u));
  uint block_offset_guest =
      XeTextureLoadSourceAddress(load_info, block_index, 4u);
  uint i;
  unroll_xe for (i = 0u; i < 2u; ++i) {
    if (i != 0u) {
      block_offset_host += 16u;
      block_offset_guest += XeTextureLoadLocalXAddressXor(1u, 4u,
                                                          load_info.is_tiled);
    }
    uint4_xe block = XeEndianSwap32(
        byte_buffer_align16_load16_xe(xe_texture_load_source,
                                      block_offset_guest),
        load_info.endian_32);
    uint2_xe bgr_end_8in10 = XeDXTColorEndpointsToBGR8In10(block.z);
    // Sort the color indices so they can be used as weights for the second
    // endpoint.
    uint bgr_weights = XeDXTHighColorWeights(block.w);
    byte_buffer_align16_store16_xe(
        xe_texture_load_dest, block_offset_host,
        XeDXTOpaqueRowToRGB8(bgr_end_8in10, bgr_weights) +
            ((block.xxxx >> uint4_xe(0u, 4u, 8u, 12u)) & 0xFu) * 0x11000000u);
    dont_flatten_xe if (texel_index_host.y + 1u < load_info.height_texels) {
      byte_buffer_align16_store16_xe(
          xe_texture_load_dest, block_offset_host + load_info.host_pitch,
          XeDXTOpaqueRowToRGB8(bgr_end_8in10, bgr_weights >> 8u) +
              ((block.xxxx >> uint4_xe(16u, 20u, 24u, 28u)) & 0xFu) *
              0x11000000u);
      dont_flatten_xe if (texel_index_host.y + 2u < load_info.height_texels) {
        byte_buffer_align16_store16_xe(
            xe_texture_load_dest, block_offset_host + 2u * load_info.host_pitch,
            XeDXTOpaqueRowToRGB8(bgr_end_8in10, bgr_weights >> 16u) +
                ((block.yyyy >> uint4_xe(0u, 4u, 8u, 12u)) & 0xFu) *
                0x11000000u);
        dont_flatten_xe
        if (texel_index_host.y + 3u < load_info.height_texels) {
          byte_buffer_align16_store16_xe(
              xe_texture_load_dest,
              block_offset_host + 3u * load_info.host_pitch,
              XeDXTOpaqueRowToRGB8(bgr_end_8in10, bgr_weights >> 24u) +
                  ((block.yyyy >> uint4_xe(16u, 20u, 24u, 28u)) & 0xFu) *
                  0x11000000u);
        }
      }
    }
  }
}
entry_code_end_compute_xe
