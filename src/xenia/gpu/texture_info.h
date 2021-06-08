/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_TEXTURE_INFO_H_
#define XENIA_GPU_TEXTURE_INFO_H_

#include <cstring>
#include <memory>

#include "xenia/base/assert.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {

inline xenos::TextureFormat GetBaseFormat(xenos::TextureFormat texture_format) {
  // These formats are used for resampling textures / gamma control.
  switch (texture_format) {
    case xenos::TextureFormat::k_16_EXPAND:
      return xenos::TextureFormat::k_16_FLOAT;
    case xenos::TextureFormat::k_16_16_EXPAND:
      return xenos::TextureFormat::k_16_16_FLOAT;
    case xenos::TextureFormat::k_16_16_16_16_EXPAND:
      return xenos::TextureFormat::k_16_16_16_16_FLOAT;
    case xenos::TextureFormat::k_8_8_8_8_AS_16_16_16_16:
      return xenos::TextureFormat::k_8_8_8_8;
    case xenos::TextureFormat::k_DXT1_AS_16_16_16_16:
      return xenos::TextureFormat::k_DXT1;
    case xenos::TextureFormat::k_DXT2_3_AS_16_16_16_16:
      return xenos::TextureFormat::k_DXT2_3;
    case xenos::TextureFormat::k_DXT4_5_AS_16_16_16_16:
      return xenos::TextureFormat::k_DXT4_5;
    case xenos::TextureFormat::k_2_10_10_10_AS_16_16_16_16:
      return xenos::TextureFormat::k_2_10_10_10;
    case xenos::TextureFormat::k_10_11_11_AS_16_16_16_16:
      return xenos::TextureFormat::k_10_11_11;
    case xenos::TextureFormat::k_11_11_10_AS_16_16_16_16:
      return xenos::TextureFormat::k_11_11_10;
    case xenos::TextureFormat::k_8_8_8_8_GAMMA_EDRAM:
      return xenos::TextureFormat::k_8_8_8_8;
    default:
      break;
  }

  return texture_format;
}

inline size_t GetTexelSize(xenos::TextureFormat format) {
  switch (format) {
    case xenos::TextureFormat::k_1_5_5_5:
      return 2;
    case xenos::TextureFormat::k_2_10_10_10:
      return 4;
    case xenos::TextureFormat::k_4_4_4_4:
      return 2;
    case xenos::TextureFormat::k_5_6_5:
      return 2;
    case xenos::TextureFormat::k_8:
      return 1;
    case xenos::TextureFormat::k_8_8:
      return 2;
    case xenos::TextureFormat::k_8_8_8_8:
      return 4;
    case xenos::TextureFormat::k_16:
      return 4;
    case xenos::TextureFormat::k_16_FLOAT:
      return 4;
    case xenos::TextureFormat::k_16_16:
      return 4;
    case xenos::TextureFormat::k_16_16_FLOAT:
      return 4;
    case xenos::TextureFormat::k_16_16_16_16:
      return 8;
    case xenos::TextureFormat::k_16_16_16_16_FLOAT:
      return 8;
    case xenos::TextureFormat::k_32_FLOAT:
      return 4;
    case xenos::TextureFormat::k_32_32_FLOAT:
      return 8;
    case xenos::TextureFormat::k_32_32_32_32_FLOAT:
      return 16;
    case xenos::TextureFormat::k_10_11_11:
    case xenos::TextureFormat::k_11_11_10:
      return 4;
    default:
      assert_unhandled_case(format);
      return 0;
  }
}

inline xenos::TextureFormat ColorFormatToTextureFormat(
    xenos::ColorFormat color_format) {
  return static_cast<xenos::TextureFormat>(color_format);
}

inline xenos::TextureFormat DepthRenderTargetToTextureFormat(
    xenos::DepthRenderTargetFormat depth_format) {
  switch (depth_format) {
    case xenos::DepthRenderTargetFormat::kD24S8:
      return xenos::TextureFormat::k_24_8;
    case xenos::DepthRenderTargetFormat::kD24FS8:
      return xenos::TextureFormat::k_24_8_FLOAT;
    default:
      assert_unhandled_case(depth_format);
      return xenos::TextureFormat(~0);
  }
}

enum class FormatType {
  // Uncompressed, and is also a ColorFormat.
  kResolvable,
  // Uncompressed, but resolve or memory export cannot be done to the format.
  kUncompressed,
  kCompressed,
};

struct FormatInfo {
  xenos::TextureFormat format;
  const char* name;
  FormatType type;
  uint32_t block_width;
  uint32_t block_height;
  uint32_t bits_per_pixel;

  uint32_t bytes_per_block() const {
    return block_width * block_height * bits_per_pixel / 8;
  }

  static const FormatInfo* Get(uint32_t gpu_format);

  static const FormatInfo* Get(xenos::TextureFormat format) {
    return Get(static_cast<uint32_t>(format));
  }
};

struct TextureInfo;

struct TextureExtent {
  uint32_t pitch;          // texel pitch
  uint32_t height;         // texel height
  uint32_t block_width;    // # of horizontal visible blocks
  uint32_t block_height;   // # of vertical visible blocks
  uint32_t block_pitch_h;  // # of horizontal pitch blocks
  uint32_t block_pitch_v;  // # of vertical pitch blocks
  uint32_t depth;

  uint32_t all_blocks() const { return block_pitch_h * block_pitch_v * depth; }
  uint32_t visible_blocks() const {
    return block_pitch_h * block_height * depth;
  }

  static TextureExtent Calculate(const FormatInfo* format_info, uint32_t pitch,
                                 uint32_t height, uint32_t depth, bool is_tiled,
                                 bool is_guest);
  static TextureExtent Calculate(const TextureInfo* texture_info,
                                 bool is_guest);
};

struct TextureMemoryInfo {
  uint32_t base_address;
  uint32_t base_size;
  uint32_t mip_address;
  uint32_t mip_size;
};

struct TextureInfo {
  xenos::TextureFormat format;
  xenos::Endian endianness;

  xenos::DataDimension dimension;
  uint32_t width;   // width in pixels
  uint32_t height;  // height in pixels
  uint32_t depth;   // depth in layers
  uint32_t pitch;   // pitch in blocks
  uint32_t mip_min_level;
  uint32_t mip_max_level;
  bool is_stacked;
  bool is_tiled;
  bool has_packed_mips;

  TextureMemoryInfo memory;
  TextureExtent extent;

  const FormatInfo* format_info() const {
    return FormatInfo::Get(static_cast<uint32_t>(format));
  }

  bool is_compressed() const {
    return format_info()->type == FormatType::kCompressed;
  }

  uint32_t mip_levels() const { return 1 + (mip_max_level - mip_min_level); }

  static bool Prepare(const xenos::xe_gpu_texture_fetch_t& fetch,
                      TextureInfo* out_info);

  static bool PrepareResolve(uint32_t physical_address,
                             xenos::TextureFormat texture_format,
                             xenos::Endian endian, uint32_t pitch,
                             uint32_t width, uint32_t height, uint32_t depth,
                             TextureInfo* out_info);

  uint32_t GetMaxMipLevels() const;

  const TextureExtent GetMipExtent(uint32_t mip, bool is_guest) const;

  void GetMipSize(uint32_t mip, uint32_t* width, uint32_t* height) const;

  // Get the memory location of a mip. offset_x and offset_y are in blocks.
  uint32_t GetMipLocation(uint32_t mip, uint32_t* offset_x, uint32_t* offset_y,
                          bool is_guest) const;

  static bool GetPackedTileOffset(uint32_t width, uint32_t height,
                                  const FormatInfo* format_info,
                                  int packed_tile, uint32_t* offset_x,
                                  uint32_t* offset_y);

  bool GetPackedTileOffset(int packed_tile, uint32_t* offset_x,
                           uint32_t* offset_y) const;

  uint64_t hash() const;
  bool operator==(const TextureInfo& other) const {
    return std::memcmp(this, &other, sizeof(TextureInfo)) == 0;
  }

 private:
  void SetupMemoryInfo(uint32_t base_address, uint32_t mip_address);
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TEXTURE_INFO_H_
