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

#include <array>
#include <cstring>
#include <memory>
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
#if XE_ARCH_AMD64 == 1
struct GetBaseFormatHelper {
  uint64_t indexer_;
  // chrispy: todo, can encode deltas or a SHIFT+ADD for remapping the input
  // format to the base, the shuffle lookup isnt great
  std::array<int8_t, 16> remap_;
};
constexpr GetBaseFormatHelper PrecomputeGetBaseFormatTable() {
#define R(x, y) xenos::TextureFormat::x, xenos::TextureFormat::y
  constexpr xenos::TextureFormat entries[] = {
      R(k_16_EXPAND, k_16_FLOAT),
      R(k_16_16_EXPAND, k_16_16_FLOAT),
      R(k_16_16_16_16_EXPAND, k_16_16_16_16_FLOAT),
      R(k_8_8_8_8_AS_16_16_16_16, k_8_8_8_8),
      R(k_DXT1_AS_16_16_16_16, k_DXT1),
      R(k_DXT2_3_AS_16_16_16_16, k_DXT2_3),
      R(k_DXT4_5_AS_16_16_16_16, k_DXT4_5),
      R(k_2_10_10_10_AS_16_16_16_16, k_2_10_10_10),
      R(k_10_11_11_AS_16_16_16_16, k_10_11_11),
      R(k_11_11_10_AS_16_16_16_16, k_11_11_10),
      R(k_8_8_8_8_GAMMA_EDRAM, k_8_8_8_8)};

#undef R

  uint64_t need_remap_table = 0ULL;
  constexpr unsigned num_entries = sizeof(entries) / sizeof(entries[0]);

  for (unsigned i = 0; i < num_entries / 2; ++i) {
    need_remap_table |= 1ULL << static_cast<uint32_t>(entries[i * 2]);
  }
  std::array<int8_t, 16> remap{0};

  for (unsigned i = 0; i < num_entries / 2; ++i) {
    remap[i] = static_cast<int8_t>(static_cast<uint32_t>(entries[(i * 2) + 1]));
  }

  return GetBaseFormatHelper{need_remap_table, remap};
}
inline xenos::TextureFormat GetBaseFormat(xenos::TextureFormat texture_format) {
  constexpr GetBaseFormatHelper helper = PrecomputeGetBaseFormatTable();

  constexpr uint64_t indexer_table = helper.indexer_;
  constexpr std::array<int8_t, 16> table = helper.remap_;

  uint64_t format_mask = 1ULL << static_cast<uint32_t>(texture_format);
  if ((indexer_table & format_mask)) {
    uint64_t trailing_mask = format_mask - 1ULL;
    uint64_t trailing_bits = indexer_table & trailing_mask;

    uint32_t sparse_index = xe::bit_count(trailing_bits);

    __m128i index_in_low =
        _mm_cvtsi32_si128(static_cast<int>(sparse_index) | 0x80808000);

    __m128i new_format_low = _mm_shuffle_epi8(
        _mm_setr_epi8(table[0], table[1], table[2], table[3], table[4],
                      table[5], table[6], table[7], table[8], table[9],
                      table[10], table[11], table[12], 0, 0, 0),
        index_in_low);

    uint32_t prelaundered =
        static_cast<uint32_t>(_mm_cvtsi128_si32(new_format_low));
    return *reinterpret_cast<xenos::TextureFormat*>(&prelaundered);

  } else {
    return texture_format;
  }
}
#else
inline xenos::TextureFormat GetBaseFormat(xenos::TextureFormat texture_format) {
  // These formats are used for resampling textures / gamma control.
  // 11 entries
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
#endif
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

enum class FormatType : uint32_t {
  // Uncompressed, and is also a ColorFormat.
  kResolvable,
  // Uncompressed, but resolve or memory export cannot be done to the format.
  kUncompressed,
  kCompressed,
};

struct FormatInfo {
  const xenos::TextureFormat format;

  const FormatType type;
  const uint32_t block_width;
  const uint32_t block_height;
  const uint32_t bits_per_pixel;

  uint32_t bytes_per_block() const {
    return block_width * block_height * bits_per_pixel / 8;
  }

  static const FormatInfo* Get(uint32_t gpu_format);

  static const char* GetName(uint32_t gpu_format);
  static const char* GetName(xenos::TextureFormat format) {
    return GetName(static_cast<uint32_t>(format));
  }

  static unsigned char GetWidthShift(uint32_t gpu_format);
  static unsigned char GetHeightShift(uint32_t gpu_format);

  static unsigned char GetWidthShift(xenos::TextureFormat gpu_format) {
    return GetWidthShift(static_cast<uint32_t>(gpu_format));
  }
  static unsigned char GetHeightShift(xenos::TextureFormat gpu_format) {
    return GetHeightShift(static_cast<uint32_t>(gpu_format));
  }
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
  const char* format_name() const {
    return FormatInfo::GetName(static_cast<uint32_t>(format));
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
