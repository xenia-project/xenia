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

// a2xx_sq_surfaceformat
enum class TextureFormat : uint32_t {
  k_1_REVERSE = 0,
  k_1 = 1,
  k_8 = 2,
  k_1_5_5_5 = 3,
  k_5_6_5 = 4,
  k_6_5_5 = 5,
  k_8_8_8_8 = 6,
  k_2_10_10_10 = 7,
  k_8_A = 8,
  k_8_B = 9,
  k_8_8 = 10,
  k_Cr_Y1_Cb_Y0 = 11,
  k_Y1_Cr_Y0_Cb = 12,
  k_Shadow = 13,
  k_8_8_8_8_A = 14,
  k_4_4_4_4 = 15,
  k_10_11_11 = 16,
  k_11_11_10 = 17,
  k_DXT1 = 18,
  k_DXT2_3 = 19,
  k_DXT4_5 = 20,
  k_DXV = 21,
  k_24_8 = 22,
  k_24_8_FLOAT = 23,
  k_16 = 24,
  k_16_16 = 25,
  k_16_16_16_16 = 26,
  k_16_EXPAND = 27,
  k_16_16_EXPAND = 28,
  k_16_16_16_16_EXPAND = 29,
  k_16_FLOAT = 30,
  k_16_16_FLOAT = 31,
  k_16_16_16_16_FLOAT = 32,
  k_32 = 33,
  k_32_32 = 34,
  k_32_32_32_32 = 35,
  k_32_FLOAT = 36,
  k_32_32_FLOAT = 37,
  k_32_32_32_32_FLOAT = 38,
  k_32_AS_8 = 39,
  k_32_AS_8_8 = 40,
  k_16_MPEG = 41,
  k_16_16_MPEG = 42,
  k_8_INTERLACED = 43,
  k_32_AS_8_INTERLACED = 44,
  k_32_AS_8_8_INTERLACED = 45,
  k_16_INTERLACED = 46,
  k_16_MPEG_INTERLACED = 47,
  k_16_16_MPEG_INTERLACED = 48,
  k_DXN = 49,
  k_8_8_8_8_AS_16_16_16_16 = 50,
  k_DXT1_AS_16_16_16_16 = 51,
  k_DXT2_3_AS_16_16_16_16 = 52,
  k_DXT4_5_AS_16_16_16_16 = 53,
  k_2_10_10_10_AS_16_16_16_16 = 54,
  k_10_11_11_AS_16_16_16_16 = 55,
  k_11_11_10_AS_16_16_16_16 = 56,
  k_32_32_32_FLOAT = 57,
  k_DXT3A = 58,
  k_DXT5A = 59,
  k_CTX1 = 60,
  k_DXT3A_AS_1_1_1_1 = 61,
  k_2_10_10_10_FLOAT = 62,

  kUnknown = 0xFFFFFFFFu,
};

inline TextureFormat GetBaseFormat(TextureFormat texture_format) {
  // These formats are used for resampling textures / gamma control.
  switch (texture_format) {
    case TextureFormat::k_16_EXPAND:
      return TextureFormat::k_16_FLOAT;
    case TextureFormat::k_16_16_EXPAND:
      return TextureFormat::k_16_16_FLOAT;
    case TextureFormat::k_16_16_16_16_EXPAND:
      return TextureFormat::k_16_16_16_16_FLOAT;
    case TextureFormat::k_8_8_8_8_AS_16_16_16_16:
      return TextureFormat::k_8_8_8_8;
    case TextureFormat::k_DXT1_AS_16_16_16_16:
      return TextureFormat::k_DXT1;
    case TextureFormat::k_DXT2_3_AS_16_16_16_16:
      return TextureFormat::k_DXT2_3;
    case TextureFormat::k_DXT4_5_AS_16_16_16_16:
      return TextureFormat::k_DXT4_5;
    case TextureFormat::k_2_10_10_10_AS_16_16_16_16:
      return TextureFormat::k_2_10_10_10;
    case TextureFormat::k_10_11_11_AS_16_16_16_16:
      return TextureFormat::k_10_11_11;
    case TextureFormat::k_11_11_10_AS_16_16_16_16:
      return TextureFormat::k_11_11_10;
    case TextureFormat::k_DXT3A_AS_1_1_1_1:
      return TextureFormat::k_DXT3A;
    default:
      break;
  }

  return texture_format;
}

inline size_t GetTexelSize(TextureFormat format) {
  switch (format) {
    case TextureFormat::k_1_5_5_5:
      return 2;
      break;
    case TextureFormat::k_2_10_10_10:
      return 4;
      break;
    case TextureFormat::k_4_4_4_4:
      return 2;
      break;
    case TextureFormat::k_5_6_5:
      return 2;
      break;
    case TextureFormat::k_8:
      return 1;
      break;
    case TextureFormat::k_8_8:
      return 2;
      break;
    case TextureFormat::k_8_8_8_8:
      return 4;
      break;
    case TextureFormat::k_16:
      return 4;
      break;
    case TextureFormat::k_16_FLOAT:
      return 4;
      break;
    case TextureFormat::k_16_16:
      return 4;
      break;
    case TextureFormat::k_16_16_FLOAT:
      return 4;
      break;
    case TextureFormat::k_16_16_16_16:
      return 8;
      break;
    case TextureFormat::k_16_16_16_16_FLOAT:
      return 8;
      break;
    case TextureFormat::k_32_FLOAT:
      return 4;
      break;
    case TextureFormat::k_32_32_FLOAT:
      return 8;
      break;
    case TextureFormat::k_32_32_32_32_FLOAT:
      return 16;
      break;
    case TextureFormat::k_10_11_11:
    case TextureFormat::k_11_11_10:
      return 4;
      break;
    default:
      assert_unhandled_case(format);
      return 0;
  }
}

inline TextureFormat ColorFormatToTextureFormat(ColorFormat color_format) {
  return static_cast<TextureFormat>(color_format);
}

inline TextureFormat ColorRenderTargetToTextureFormat(
    ColorRenderTargetFormat color_format) {
  switch (color_format) {
    case ColorRenderTargetFormat::k_8_8_8_8:
      return TextureFormat::k_8_8_8_8;
    case ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return TextureFormat::k_8_8_8_8;
    case ColorRenderTargetFormat::k_2_10_10_10:
      return TextureFormat::k_2_10_10_10;
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
      return TextureFormat::k_2_10_10_10_FLOAT;
    case ColorRenderTargetFormat::k_16_16:
      return TextureFormat::k_16_16;
    case ColorRenderTargetFormat::k_16_16_16_16:
      return TextureFormat::k_16_16_16_16;
    case ColorRenderTargetFormat::k_16_16_FLOAT:
      return TextureFormat::k_16_16_FLOAT;
    case ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      return TextureFormat::k_16_16_16_16_FLOAT;
    case ColorRenderTargetFormat::k_2_10_10_10_unknown:
      return TextureFormat::k_2_10_10_10;
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_unknown:
      return TextureFormat::k_2_10_10_10_FLOAT;
    case ColorRenderTargetFormat::k_32_FLOAT:
      return TextureFormat::k_32_FLOAT;
    case ColorRenderTargetFormat::k_32_32_FLOAT:
      return TextureFormat::k_32_32_FLOAT;
    default:
      assert_unhandled_case(color_format);
      return TextureFormat::kUnknown;
  }
}

inline TextureFormat DepthRenderTargetToTextureFormat(
    DepthRenderTargetFormat depth_format) {
  switch (depth_format) {
    case DepthRenderTargetFormat::kD24S8:
      return TextureFormat::k_24_8;
    case DepthRenderTargetFormat::kD24FS8:
      return TextureFormat::k_24_8_FLOAT;
    default:
      assert_unhandled_case(depth_format);
      return TextureFormat::kUnknown;
  }
}

enum class FormatType {
  kUncompressed,
  kCompressed,
};

struct FormatInfo {
  TextureFormat format;
  const char* name;
  FormatType type;
  uint32_t block_width;
  uint32_t block_height;
  uint32_t bits_per_pixel;

  static const FormatInfo* Get(uint32_t gpu_format);
};

struct TextureInfo {
  uint32_t guest_address;
  TextureFormat texture_format;
  Dimension dimension;
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  Endian endianness;
  bool is_tiled;
  bool has_packed_mips;
  uint32_t input_length;

  const FormatInfo* format_info() const {
    return FormatInfo::Get(static_cast<uint32_t>(texture_format));
  }

  bool is_compressed() const {
    return format_info()->type == FormatType::kCompressed;
  }

  union {
    struct {
      uint32_t logical_width;
      uint32_t block_width;  // # of horizontal blocks
      uint32_t input_width;  // texel pitch
      uint32_t input_pitch;  // byte pitch
    } size_1d;
    struct {
      uint32_t logical_width;
      uint32_t logical_height;
      uint32_t block_width;   // # of horizontal blocks
      uint32_t block_height;  // # of vertical blocks
      uint32_t input_width;   // texel pitch
      uint32_t input_height;  // texel height
      uint32_t input_pitch;   // byte pitch
    } size_2d;
    struct {
    } size_3d;
    struct {
      uint32_t logical_width;
      uint32_t logical_height;
      uint32_t block_width;        // # of horizontal blocks
      uint32_t block_height;       // # of vertical blocks
      uint32_t input_width;        // texel pitch
      uint32_t input_height;       // texel height
      uint32_t input_pitch;        // byte pitch
      uint32_t input_face_length;  // byte pitch of face
    } size_cube;
  };

  static bool Prepare(const xenos::xe_gpu_texture_fetch_t& fetch,
                      TextureInfo* out_info);

  static bool PrepareResolve(uint32_t physical_address,
                             TextureFormat texture_format, Endian endian,
                             uint32_t width, uint32_t height,
                             TextureInfo* out_info);

  static bool GetPackedTileOffset(const TextureInfo& texture_info,
                                  uint32_t* out_offset_x,
                                  uint32_t* out_offset_y);
  static uint32_t TiledOffset2DOuter(uint32_t y, uint32_t width,
                                     uint32_t log_bpp);
  static uint32_t TiledOffset2DInner(uint32_t x, uint32_t y, uint32_t bpp,
                                     uint32_t base_offset);

  uint64_t hash() const;
  bool operator==(const TextureInfo& other) const {
    return std::memcmp(this, &other, sizeof(TextureInfo)) == 0;
  }

 private:
  void CalculateTextureSizes2D(uint32_t width, uint32_t height);
  void CalculateTextureSizesCube(uint32_t width, uint32_t height,
                                 uint32_t depth);
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_TEXTURE_INFO_H_
