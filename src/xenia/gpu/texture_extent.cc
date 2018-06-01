/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/texture_info.h"

#include <algorithm>
#include <cmath>

#include "xenia/base/math.h"

namespace xe {
namespace gpu {

using namespace xe::gpu::xenos;

static TextureExtent CalculateExtent(const FormatInfo* format_info,
                                     uint32_t pitch, uint32_t height,
                                     uint32_t depth, bool is_tiled,
                                     bool is_guest) {
  TextureExtent usage;

  usage.pitch = pitch;
  usage.height = height;
  usage.block_pitch_h = xe::round_up(usage.pitch, format_info->block_width) /
                        format_info->block_width;
  usage.block_height = xe::round_up(usage.height, format_info->block_height) /
                       format_info->block_height;
  usage.block_pitch_v = usage.block_height;
  usage.depth = depth;

  if (is_guest) {
    // Texture dimensions must be a multiple of tile
    // dimensions (32x32 blocks).
    usage.block_pitch_h = xe::round_up(usage.block_pitch_h, 32);
    usage.block_pitch_v = xe::round_up(usage.block_pitch_v, 32);

    usage.pitch = usage.block_pitch_h * format_info->block_width;
    usage.height = usage.block_pitch_v * format_info->block_height;

    uint32_t bytes_per_block = format_info->bytes_per_block();
    uint32_t byte_pitch = usage.block_pitch_h * bytes_per_block;

    if (!is_tiled) {
      // Each row must be a multiple of 256 bytes in linear textures.
      byte_pitch = xe::round_up(byte_pitch, 256);
      usage.block_pitch_h = byte_pitch / bytes_per_block;
      usage.pitch = usage.block_pitch_h * format_info->block_width;
    }

    // Is depth special?
    usage.depth = usage.depth;
  }

  return usage;
}

TextureExtent TextureExtent::Calculate(const FormatInfo* format_info,
                                       uint32_t pitch, uint32_t height,
                                       uint32_t depth, bool is_tiled,
                                       bool is_guest) {
  return CalculateExtent(format_info, pitch, height, depth, is_tiled, is_guest);
}

TextureExtent TextureExtent::Calculate(const TextureInfo* info, bool is_guest) {
  assert_not_null(info);
  return CalculateExtent(info->format_info(), info->pitch, info->height + 1,
                         info->depth + 1, info->is_tiled, is_guest);
}

}  //  namespace gpu
}  //  namespace xe
