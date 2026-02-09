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
  // 1 thread = 4 DXT1 blocks to 16x4 R8G8B8A8 texels.
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
                                     load_info.height_texels, 4u));
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
  uint4_xe end_8in10_01 =
      uint4_xe(XeDXTColorEndpointsToBGR8In10(blocks_01.x),
               XeDXTColorEndpointsToBGR8In10(blocks_01.z));
  uint4_xe end_8in10_23 =
      uint4_xe(XeDXTColorEndpointsToBGR8In10(blocks_23.x),
               XeDXTColorEndpointsToBGR8In10(blocks_23.z));
  bool4_xe is_trans =
      less_than_equal_xe(uint4_xe(end_8in10_01.xz, end_8in10_23.xz),
                         uint4_xe(end_8in10_01.yw, end_8in10_23.yw));
  uint4_xe weights = uint4_xe(blocks_01.yw, blocks_23.yw);
  weights = select_xe(is_trans, XeDXT1TransWeights(weights),
                      XeDXTHighColorWeights(weights));
  uint i;
  unroll_xe for (i = 0u; i < 4u; ++i) {
    if (i != 0u) {
      if (texel_index_host.y + i >= load_info.height_texels) {
        break;
      }
      block_offset_host += load_info.host_pitch;
      weights >>= 8u;
    }
    byte_buffer_align16_store16_xe(
        xe_texture_load_dest, block_offset_host,
        is_trans.x ? XeDXT1TransRowToRGBA8(end_8in10_01.xy, weights.x)
                   : (XeDXTOpaqueRowToRGB8(end_8in10_01.xy, weights.x) |
                      0xFF000000u));
    byte_buffer_align16_store16_xe(
        xe_texture_load_dest, block_offset_host + 0x10u,
        is_trans.y ? XeDXT1TransRowToRGBA8(end_8in10_01.zw, weights.y)
                   : (XeDXTOpaqueRowToRGB8(end_8in10_01.zw, weights.y) |
                      0xFF000000u));
    byte_buffer_align16_store16_xe(
        xe_texture_load_dest, block_offset_host + 0x20u,
        is_trans.z ? XeDXT1TransRowToRGBA8(end_8in10_23.xy, weights.z)
                   : (XeDXTOpaqueRowToRGB8(end_8in10_23.xy, weights.z) |
                      0xFF000000u));
    byte_buffer_align16_store16_xe(
        xe_texture_load_dest, block_offset_host + 0x30u,
        is_trans.w ? XeDXT1TransRowToRGBA8(end_8in10_23.zw, weights.w)
                   : (XeDXTOpaqueRowToRGB8(end_8in10_23.zw, weights.w) |
                      0xFF000000u));
  }
}
entry_code_end_compute_xe
