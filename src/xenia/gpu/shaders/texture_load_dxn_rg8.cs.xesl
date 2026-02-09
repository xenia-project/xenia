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
  // 1 thread = 2 DXN blocks to 8x4 R8G8 texels.
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
                                     load_info.height_texels, 2u));
  uint block_offset_guest =
      XeTextureLoadSourceAddress(load_info, block_index, 4u);
  uint4_xe block_0 = XeEndianSwap32(
      byte_buffer_align16_load16_xe(xe_texture_load_source, block_offset_guest),
      load_info.endian_32);
  block_offset_guest += XeTextureLoadLocalXAddressXor(1u, 4u,
                                                      load_info.is_tiled);
  uint4_xe block_1 = XeEndianSwap32(
      byte_buffer_align16_load16_xe(xe_texture_load_source, block_offset_guest),
      load_info.endian_32);
  uint4_xe end_0 = (block_0.xxzz >> uint4_xe(0u, 8u, 0u, 8u)) & 0xFFu;
  uint4_xe end_1 = (block_1.xxzz >> uint4_xe(0u, 8u, 0u, 8u)) & 0xFFu;
  uint4_xe weights = (uint4_xe(block_0.xz, block_1.xz) >> 16u) |
                     ((uint4_xe(block_0.yw, block_1.yw) & 0xFFu) << 16u);
  weights = uint4_xe(XeDXT5HighAlphaWeights(end_0.xy, weights.x),
                     XeDXT5HighAlphaWeights(end_0.zw, weights.y),
                     XeDXT5HighAlphaWeights(end_1.xy, weights.z),
                     XeDXT5HighAlphaWeights(end_1.zw, weights.w));
  byte_buffer_align16_store16_xe(
      xe_texture_load_dest, block_offset_host,
      uint4_xe(XeDXT5RowToA8In16(end_0.xy, weights.x) |
                   (XeDXT5RowToA8In16(end_0.zw, weights.y) << 8u),
               XeDXT5RowToA8In16(end_1.xy, weights.z) |
                   (XeDXT5RowToA8In16(end_1.zw, weights.w) << 8u)));
  dont_flatten_xe if (++texel_index_host.y < load_info.height_texels) {
    block_offset_host += load_info.host_pitch;
    weights >>= 12u;
    byte_buffer_align16_store16_xe(
        xe_texture_load_dest, block_offset_host,
        uint4_xe(XeDXT5RowToA8In16(end_0.xy, weights.x) |
                     (XeDXT5RowToA8In16(end_0.zw, weights.y) << 8u),
                 XeDXT5RowToA8In16(end_1.xy, weights.z) |
                     (XeDXT5RowToA8In16(end_1.zw, weights.w) << 8u)));
    dont_flatten_xe if (++texel_index_host.y < load_info.height_texels) {
      block_offset_host += load_info.host_pitch;
      weights = uint4_xe(block_0.yw, block_1.yw) >> 8u;
      weights = uint4_xe(XeDXT5HighAlphaWeights(end_0.xy, weights.x),
                         XeDXT5HighAlphaWeights(end_0.zw, weights.y),
                         XeDXT5HighAlphaWeights(end_1.xy, weights.z),
                         XeDXT5HighAlphaWeights(end_1.zw, weights.w));
      byte_buffer_align16_store16_xe(
          xe_texture_load_dest, block_offset_host,
          uint4_xe(XeDXT5RowToA8In16(end_0.xy, weights.x) |
                       (XeDXT5RowToA8In16(end_0.zw, weights.y) << 8u),
                   XeDXT5RowToA8In16(end_1.xy, weights.z) |
                       (XeDXT5RowToA8In16(end_1.zw, weights.w) << 8u)));
      dont_flatten_xe if (++texel_index_host.y < load_info.height_texels) {
        block_offset_host += load_info.host_pitch;
        weights >>= 12u;
        byte_buffer_align16_store16_xe(
            xe_texture_load_dest, block_offset_host,
            uint4_xe(XeDXT5RowToA8In16(end_0.xy, weights.x) |
                         (XeDXT5RowToA8In16(end_0.zw, weights.y) << 8u),
                     XeDXT5RowToA8In16(end_1.xy, weights.z) |
                         (XeDXT5RowToA8In16(end_1.zw, weights.w) << 8u)));
      }
    }
  }
}
entry_code_end_compute_xe
