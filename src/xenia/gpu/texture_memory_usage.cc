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

static TextureMemoryUsage CalculateMemoryUsage(const FormatInfo* format_info,
                                               uint32_t pitch, uint32_t height,
                                               uint32_t depth, bool is_tiled,
                                               bool is_guest) {
  TextureMemoryUsage usage;

  usage.pitch = pitch;
  usage.height = height;
  usage.block_pitch = xe::round_up(usage.pitch, format_info->block_width) /
                      format_info->block_width;
  usage.block_height = xe::round_up(usage.height, format_info->block_height) /
                       format_info->block_height;
  usage.depth = depth;

  if (is_guest) {
    // Texture dimensions must be a multiple of tile
    // dimensions (32x32 blocks).
    usage.block_pitch = xe::round_up(usage.block_pitch, 32);
    usage.block_height = xe::round_up(usage.block_height, 32);

    usage.pitch = usage.block_pitch * format_info->block_width;
    usage.height = usage.block_height * format_info->block_height;

    uint32_t bytes_per_block = format_info->bytes_per_block();
    uint32_t byte_pitch = usage.block_pitch * bytes_per_block;

    if (!is_tiled) {
      // Each row must be a multiple of 256 bytes in linear textures.
      byte_pitch = xe::round_up(byte_pitch, 256);
      usage.block_pitch = byte_pitch / bytes_per_block;
      usage.pitch = usage.block_pitch * format_info->block_width;
    }

    // Is depth special?
    usage.depth = usage.depth;
  }

  return usage;
}

TextureMemoryUsage TextureMemoryUsage::Calculate(const FormatInfo* format_info,
                                                 uint32_t pitch,
                                                 uint32_t height,
                                                 uint32_t depth, bool is_tiled,
                                                 bool is_guest) {
  return CalculateMemoryUsage(format_info, pitch, height, depth, is_tiled,
                              is_guest);
}

TextureMemoryUsage TextureMemoryUsage::Calculate(const TextureInfo* info,
                                                 bool is_guest) {
  assert_not_null(info);
  return CalculateMemoryUsage(info->format_info(), info->pitch,
                              info->height + 1, info->depth + 1, info->is_tiled,
                              is_guest);
}

}  //  namespace gpu
}  //  namespace xe
