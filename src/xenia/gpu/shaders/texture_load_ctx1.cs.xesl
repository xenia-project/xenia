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

// http://fileadmin.cs.lth.se/cs/Personal/Michael_Doggett/talks/unc-xenos-doggett.pdf
// CXT1 is like DXT3/5 color, but 2-component and with 8:8 endpoints rather than
// 5:6:5.
//
// Dword 1:
// rrrrrrrrgggggggg
// RRRRRRRRGGGGGGGG
// (R is in the higher bits, according to how this format is used in 4D5307E6).
// Dword 2:
// AA BB CC DD
// EE FF GG HH
// II JJ KK LL
// MM NN OO PP

XE_TEXTURE_LOAD_ENTRY(16)
{
  // 1 thread = 4 CTX1 blocks to 16x4 R8G8 texels.
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
                                     load_info.height_texels, 2u));
  uint block_offset_guest =
      XeTextureLoadSourceAddress(load_info, block_index, 3u);
  uint i;
  unroll_xe for (i = 0u; i < 2u; ++i) {
    if (i != 0u) {
      block_offset_host += 16u;
      block_offset_guest += XeTextureLoadLocalXAddressXor(2u, 3u,
                                                          load_info.is_tiled);
    }
    // Two blocks.
    uint4_xe blocks = XeEndianSwap32(
        byte_buffer_align16_load16_xe(xe_texture_load_source,
                                      block_offset_guest),
        load_info.endian_32);
    // Unpack the endpoints as 0x00g000r0 0x00G000R0 0x00g100r1 0x00G100R1 so
    // they can be multiplied by their weights allowing overflow.
    uint4_xe end_8in16;
    end_8in16.xz = ((blocks.xz >> 8u) & 0xFFu) | ((blocks.xz & 0xFFu) << 16u);
    end_8in16.yw = (blocks.xz >> 24u) | (blocks.xz & 0xFF0000u);
    uint2_xe weights_high = XeDXTHighColorWeights(blocks.yw);
    byte_buffer_align16_store16_xe(
        xe_texture_load_dest, block_offset_host,
        XeCTX1TwoBlocksRowToR8G8(end_8in16, weights_high));
    dont_flatten_xe if (texel_index_host.y + 1u < load_info.height_texels) {
      byte_buffer_align16_store16_xe(
          xe_texture_load_dest, block_offset_host + load_info.host_pitch,
          XeCTX1TwoBlocksRowToR8G8(end_8in16, weights_high >> 8u));
      dont_flatten_xe if (texel_index_host.y + 2u < load_info.height_texels) {
        byte_buffer_align16_store16_xe(
            xe_texture_load_dest, block_offset_host + 2u * load_info.host_pitch,
            XeCTX1TwoBlocksRowToR8G8(end_8in16, weights_high >> 16u));
        dont_flatten_xe
        if (texel_index_host.y + 3u < load_info.height_texels) {
          byte_buffer_align16_store16_xe(
              xe_texture_load_dest,
              block_offset_host + 3u * load_info.host_pitch,
              XeCTX1TwoBlocksRowToR8G8(end_8in16, weights_high >> 24u));
        }
      }
    }
  }
}
entry_code_end_compute_xe
