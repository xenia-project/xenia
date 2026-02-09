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
  // 1 thread = 4 DXT3A blocks to 16x4 R8 texels (no need to convert to DXT3
  // because the overhead is the same, 2x, but the size must be 4-aligned on
  // Direct3D 12).
  XeTextureLoadInfo load_info = XeTextureLoadGetInfo(pass_push_consts_xe);
  uint3_xe block_index = in_global_thread_id_xe << uint3_xe(2u, 0u, 0u);
  dont_flatten_xe
  if (any(greater_than_equal_xe(block_index.xy, load_info.size_blocks.xy))) {
    return;
  }
  uint3_xe texel_index_host = block_index << uint3_xe(2u, 2u, 0u);
  uint block_offset_host =
      load_info.host_offset +
      uint(XeTextureHostLinearOffset(int3_xe(texel_index_host),
                                     load_info.host_pitch,
                                     load_info.height_texels, 1u));
  uint block_offset_guest =
      XeTextureLoadSourceAddress(load_info, block_index, 3u);
  uint4_xe blocks_01 = XeEndianSwap32(
      byte_buffer_align16_load16_xe(xe_texture_load_source, block_offset_guest),
      load_info.endian_32);
  block_offset_guest += XeTextureLoadLocalXAddressXor(2u, 3u,
                                                      load_info.is_tiled);
  uint4_xe blocks_23 = XeEndianSwap32(
      byte_buffer_align16_load16_xe(xe_texture_load_source, block_offset_guest),
      load_info.endian_32);
  byte_buffer_align16_store16_xe(
      xe_texture_load_dest, block_offset_host,
      XeDXT3FourBlocksRowToA8(uint4_xe(blocks_01.xz, blocks_23.xz)));
  dont_flatten_xe if (++texel_index_host.y < load_info.height_texels) {
    block_offset_host += load_info.host_pitch;
    byte_buffer_align16_store16_xe(
        xe_texture_load_dest, block_offset_host,
        XeDXT3FourBlocksRowToA8(uint4_xe(blocks_01.xz, blocks_23.xz) >> 16u));
    dont_flatten_xe if (++texel_index_host.y < load_info.height_texels) {
      block_offset_host += load_info.host_pitch;
      byte_buffer_align16_store16_xe(
          xe_texture_load_dest, block_offset_host,
          XeDXT3FourBlocksRowToA8(uint4_xe(blocks_01.yw, blocks_23.yw)));
      dont_flatten_xe if (++texel_index_host.y < load_info.height_texels) {
        block_offset_host += load_info.host_pitch;
        byte_buffer_align16_store16_xe(
            xe_texture_load_dest, block_offset_host,
            XeDXT3FourBlocksRowToA8(
                uint4_xe(blocks_01.yw, blocks_23.yw) >> 16u));
      }
    }
  }
}
entry_code_end_compute_xe
