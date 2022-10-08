/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/texture_info.h"
#include "xenia/base/logging.h"
#include "xenia/base/xxhash.h"

namespace xe {
namespace gpu {

using namespace xe::gpu::xenos;

bool TextureInfo::Prepare(const xe_gpu_texture_fetch_t& fetch,
                          TextureInfo* out_info) {
  // https://msdn.microsoft.com/en-us/library/windows/desktop/cc308051(v=vs.85).aspx
  // a2xx_sq_surfaceformat

  std::memset(out_info, 0, sizeof(TextureInfo));

  auto& info = *out_info;

  info.format = fetch.format;
  info.endianness = fetch.endianness;

  info.dimension = fetch.dimension;
  info.width = info.height = info.depth = 0;
  info.is_stacked = false;
  switch (info.dimension) {
    case xenos::DataDimension::k1D:
      // we treat 1D textures as 2D
      info.dimension = DataDimension::k2DOrStacked;
      info.width = fetch.size_1d.width;
      assert_true(!fetch.stacked);
      break;
    case xenos::DataDimension::k2DOrStacked:
      info.width = fetch.size_2d.width;
      info.height = fetch.size_2d.height;
      if (fetch.stacked) {
        info.depth = fetch.size_2d.stack_depth;
        info.is_stacked = true;
      }
      break;
    case xenos::DataDimension::k3D:
      info.width = fetch.size_3d.width;
      info.height = fetch.size_3d.height;
      info.depth = fetch.size_3d.depth;
      assert_true(!fetch.stacked);
      break;
    case xenos::DataDimension::kCube:
      info.width = fetch.size_2d.width;
      info.height = fetch.size_2d.height;
      assert_true(fetch.size_2d.stack_depth == 5);
      info.depth = fetch.size_2d.stack_depth;
      assert_true(!fetch.stacked);
      break;
    default:
      assert_unhandled_case(info.dimension);
      break;
  }
  info.pitch = fetch.pitch << 5;

  info.mip_min_level = fetch.mip_min_level;
  info.mip_max_level =
      std::max(uint32_t(fetch.mip_min_level), uint32_t(fetch.mip_max_level));

  info.is_tiled = fetch.tiled;
  info.has_packed_mips = fetch.packed_mips;

  info.extent = TextureExtent::Calculate(out_info, true);
  info.SetupMemoryInfo(fetch.base_address << 12, fetch.mip_address << 12);

  // We've gotten this far and mip_address is zero, assume no extra mips.
  if (info.mip_max_level > 0 && !info.memory.mip_address) {
    info.mip_max_level = 0;
  }

  assert_true(info.mip_min_level <= info.mip_max_level);

  return true;
}

bool TextureInfo::PrepareResolve(uint32_t physical_address,
                                 xenos::TextureFormat format,
                                 xenos::Endian endian, uint32_t pitch,
                                 uint32_t width, uint32_t height,
                                 uint32_t depth, TextureInfo* out_info) {
  assert_true(width > 0);
  assert_true(height > 0);

  std::memset(out_info, 0, sizeof(TextureInfo));

  auto& info = *out_info;
  info.format = format;
  info.endianness = endian;

  info.dimension = xenos::DataDimension::k2DOrStacked;
  info.width = width - 1;
  info.height = height - 1;
  info.depth = depth - 1;

  info.pitch = pitch;
  info.mip_min_level = 0;
  info.mip_max_level = 0;

  info.is_tiled = true;
  info.has_packed_mips = false;

  info.extent = TextureExtent::Calculate(out_info, true);
  info.SetupMemoryInfo(physical_address, 0);
  return true;
}

uint32_t TextureInfo::GetMaxMipLevels() const {
  return 1 + xe::log2_floor(std::max({width + 1, height + 1, depth + 1}));
}

const TextureExtent TextureInfo::GetMipExtent(uint32_t mip,
                                              bool is_guest) const {
  if (mip == 0) {
    return extent;
  }
  uint32_t mip_width, mip_height;
  if (is_guest) {
    mip_width = xe::next_pow2(width + 1) >> mip;
    mip_height = xe::next_pow2(height + 1) >> mip;
  } else {
    mip_width = std::max(1u, (width + 1) >> mip);
    mip_height = std::max(1u, (height + 1) >> mip);
  }
  return TextureExtent::Calculate(format_info(), mip_width, mip_height,
                                  depth + 1, is_tiled, is_guest);
}

void TextureInfo::GetMipSize(uint32_t mip, uint32_t* out_width,
                             uint32_t* out_height) const {
  assert_not_null(out_width);
  assert_not_null(out_height);
  if (mip == 0) {
    *out_width = width + 1;
    *out_height = height + 1;
    return;
  }
  uint32_t width_pow2 = xe::next_pow2(width + 1);
  uint32_t height_pow2 = xe::next_pow2(height + 1);
  *out_width = std::max(width_pow2 >> mip, 1u);
  *out_height = std::max(height_pow2 >> mip, 1u);
}

uint64_t TextureInfo::hash() const {
  return XXH3_64bits(this, sizeof(TextureInfo));
}

void TextureInfo::SetupMemoryInfo(uint32_t base_address, uint32_t mip_address) {
  uint32_t bytes_per_block = format_info()->bytes_per_block();

  memory.base_address = 0;
  memory.base_size = 0;
  memory.mip_address = 0;
  memory.mip_size = 0;

  if (mip_min_level == 0 && base_address) {
    // There is a base mip level.
    memory.base_address = base_address;
    memory.base_size = GetMipExtent(0, true).visible_blocks() * bytes_per_block;
  }

  if (mip_min_level == 0 && mip_max_level == 0) {
    // Sort circuit. Only one mip.
    return;
  }

  if (mip_min_level == 0 && base_address == mip_address) {
    // TODO(gibbed): This doesn't actually make any sense. Force only one mip.
    // Offending title issues: #26, #45
    return;
  }

  if (mip_min_level > 0) {
    if ((base_address && !mip_address) || (base_address == mip_address)) {
      // Mip data is actually at base address?
      mip_address = base_address;
      base_address = 0;
    } else if (!base_address && mip_address) {
      // Nothing needs to be done.
    } else {
      // WTF?
      assert_always();
    }
  }

  memory.mip_address = mip_address;

  if (!has_packed_mips) {
    for (uint32_t mip = std::max(1u, mip_min_level); mip < mip_max_level;
         mip++) {
      memory.mip_size += GetMipExtent(mip, true).all_blocks() * bytes_per_block;
    }
    memory.mip_size +=
        GetMipExtent(mip_max_level, true).visible_blocks() * bytes_per_block;
    return;
  }

  uint32_t width_pow2 = xe::next_pow2(width + 1);
  uint32_t height_pow2 = xe::next_pow2(height + 1);

  // Walk forward to find the address of the mip.
  uint32_t packed_mip_base = std::max(1u, mip_min_level);
  for (uint32_t mip = packed_mip_base; mip < mip_max_level;
       mip++, packed_mip_base++) {
    uint32_t mip_width = std::max(width_pow2 >> mip, 1u);
    uint32_t mip_height = std::max(height_pow2 >> mip, 1u);
    if (std::min(mip_width, mip_height) <= 16) {
      // We've reached the point where the mips are packed into a single tile.
      break;
    }
    memory.mip_size += GetMipExtent(mip, true).all_blocks() * bytes_per_block;
  }
}

}  //  namespace gpu
}  //  namespace xe
