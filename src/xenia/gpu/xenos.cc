/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
namespace xenos {

// Based on X360GammaToLinear and X360LinearToGamma from the Source Engine, with
// additional logic from Direct3D 9 code in game executable disassembly, located
// via the floating-point constants involved.
// https://github.com/ValveSoftware/source-sdk-2013/blob/master/mp/src/mathlib/color_conversion.cpp#L329
// These are provided here in part as a reference for shader translators.

float PWLGammaToLinear(float gamma) {
  // Not found in game executables, so just using the logic similar to that in
  // the Source Engine.
  gamma = xe::saturate_unsigned(gamma);
  float scale, offset;
  // While the compiled code for linear to gamma conversion uses `vcmpgtfp
  // constant, value` comparison (constant > value, or value < constant), it's
  // preferable to use `value >= constant` condition for the higher pieces, as
  // it will never pass for NaN, and in case of NaN, the 0...64/255 case will be
  // selected regardless of whether it's saturated before or after the
  // comparisons (always pre-saturating here, but shader translators may choose
  // to saturate later for convenience), as saturation will flush NaN to 0.
  if (gamma >= 96.0f / 255.0f) {
    if (gamma >= 192.0f / 255.0f) {
      scale = 8.0f / 1024.0f;
      offset = -1024.0f;
    } else {
      scale = 4.0f / 1024.0f;
      offset = -256.0f;
    }
  } else {
    if (gamma >= 64.0f / 255.0f) {
      scale = 2.0f / 1024.0f;
      offset = -64.0f;
    } else {
      scale = 1.0f / 1024.0f;
      offset = 0.0f;
      // No `floor` term in this case in the Source Engine, but for the largest
      // value, 1.0, `floor(255.0f * (1.0f / 1024.0f))` is 0 anyway.
    }
  }
  // Though in the Source Engine, the 1/1024 multiplication is done for the
  // truncated part specifically, pre-baking it into the scale is lossless -
  // both 1024 and `scale` are powers of 2.
  float linear = gamma * ((255.0f * 1024.0f) * scale) + offset;
  // For consistency with linear to gamma, and because it's more logical here
  // (0 rather than 1 at -epsilon), using `trunc` instead of `floor`.
  linear += std::trunc(linear * scale);
  linear *= 1.0f / 1023.0f;
  // Clamping is not necessary (1 * (255 * 8) - 1024 + 7 is exactly 1023).
  return linear;
}

float LinearToPWLGamma(float linear) {
  linear = xe::saturate_unsigned(linear);
  float scale, offset;
  // While the compiled code uses `vcmpgtfp constant, value` comparison
  // (constant > value, or value < constant), it's preferable to use `value >=
  // constant` condition for the higher pieces, as it will never pass for NaN,
  // and in case of NaN, the 0...64/1023 case will be selected regardless of
  // whether it's saturated before or after the comparisons (always
  // pre-saturating here, but shader translators may choose to saturate later
  // for convenience), as saturation will flush NaN to 0.
  if (linear >= 128.0f / 1023.0f) {
    if (linear >= 512.0f / 1023.0f) {
      scale = 1023.0f / 8.0f;
      offset = 128.0f / 255.0f;
    } else {
      scale = 1023.0f / 4.0f;
      offset = 64.0f / 255.0f;
    }
  } else {
    if (linear >= 64.0f / 1023.0f) {
      scale = 1023.0f / 2.0f;
      offset = 32.0f / 255.0f;
    } else {
      scale = 1023.0f;
      offset = 0.0f;
    }
  }
  // The truncation isn't in X360LinearToGamma in the Source Engine, but is
  // there in Direct3D 9 disassembly (the `vrfiz` instructions).
  // It also prevents conversion of 1.0 to 1.0034313725490196078431372549016
  // that's handled via clamping in the Source Engine.
  // 127.875 (1023 / 8) is truncated to 127, which, after scaling, becomes
  // 127 / 255, and when 128 / 255 is added, the result is 1.
  return std::trunc(linear * scale) * (1.0f / 255.0f) + offset;
}

// https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp

float Float7e3To32(uint32_t f10) {
  f10 &= 0x3FF;
  if (!f10) {
    return 0.0f;
  }
  uint32_t mantissa = f10 & 0x7F;
  uint32_t exponent = f10 >> 7;
  if (!exponent) {
    // Normalize the value in the resulting float.
    // do { Exponent--; Mantissa <<= 1; } while ((Mantissa & 0x80) == 0)
    uint32_t mantissa_lzcnt = xe::lzcnt(mantissa) - (32 - 8);
    exponent = uint32_t(1 - int32_t(mantissa_lzcnt));
    mantissa = (mantissa << mantissa_lzcnt) & 0x7F;
  }
  uint32_t f32 = ((exponent + 124) << 23) | (mantissa << 3);
  return *reinterpret_cast<const float*>(&f32);
}

// Based on CFloat24 from d3dref9.dll and the 6e4 code from:
// https://github.com/Microsoft/DirectXTex/blob/master/DirectXTex/DirectXTexConvert.cpp
// 6e4 has a different exponent bias allowing [0,512) values, 20e4 allows [0,2).
XE_NOALIAS
uint32_t Float32To20e4(float f32, bool round_to_nearest_even) noexcept {
  if (!(f32 > 0.0f)) {
    // Positive only, and not -0 or NaN.
    return 0;
  }
  uint32_t f32u32 = *reinterpret_cast<const uint32_t*>(&f32);
  if (f32u32 >= 0x3FFFFFF8) {
    // Saturate.
    return 0xFFFFFF;
  }
  if (f32u32 < 0x38800000) {
    // The number is too small to be represented as a normalized 20e4.
    // Convert it to a denormalized value.
    uint32_t shift = std::min(uint32_t(113 - (f32u32 >> 23)), uint32_t(24));
    f32u32 = (0x800000 | (f32u32 & 0x7FFFFF)) >> shift;
  } else {
    // Rebias the exponent to represent the value as a normalized 20e4.
    f32u32 += 0xC8000000u;
  }
  if (round_to_nearest_even) {
    f32u32 += 3 + ((f32u32 >> 3) & 1);
  }
  return (f32u32 >> 3) & 0xFFFFFF;
}
XE_NOALIAS
float Float20e4To32(uint32_t f24) noexcept {
  f24 &= 0xFFFFFF;
  if (!f24) {
    return 0.0f;
  }
  uint32_t mantissa = f24 & 0xFFFFF;
  uint32_t exponent = f24 >> 20;
  if (!exponent) {
    // Normalize the value in the resulting float.
    // do { Exponent--; Mantissa <<= 1; } while ((Mantissa & 0x100000) == 0)
    uint32_t mantissa_lzcnt = xe::lzcnt(mantissa) - (32 - 21);
    exponent = uint32_t(1 - int32_t(mantissa_lzcnt));
    mantissa = (mantissa << mantissa_lzcnt) & 0xFFFFF;
  }
  uint32_t f32 = ((exponent + 112) << 23) | (mantissa << 3);
  return *reinterpret_cast<const float*>(&f32);
}

const char* GetColorRenderTargetFormatName(ColorRenderTargetFormat format) {
  switch (format) {
    case ColorRenderTargetFormat::k_8_8_8_8:
      return "k_8_8_8_8";
    case ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return "k_8_8_8_8_GAMMA";
    case ColorRenderTargetFormat::k_2_10_10_10:
      return "k_2_10_10_10";
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
      return "k_2_10_10_10_FLOAT";
    case ColorRenderTargetFormat::k_16_16:
      return "k_16_16";
    case ColorRenderTargetFormat::k_16_16_16_16:
      return "k_16_16_16_16";
    case ColorRenderTargetFormat::k_16_16_FLOAT:
      return "k_16_16_FLOAT";
    case ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      return "k_16_16_16_16_FLOAT";
    case ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
      return "k_2_10_10_10_AS_10_10_10_10";
    case ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
      return "k_2_10_10_10_FLOAT_AS_16_16_16_16";
    case ColorRenderTargetFormat::k_32_FLOAT:
      return "k_32_FLOAT";
    case ColorRenderTargetFormat::k_32_32_FLOAT:
      return "k_32_32_FLOAT";
    default:
      return "kUnknown";
  }
}

const char* GetDepthRenderTargetFormatName(DepthRenderTargetFormat format) {
  switch (format) {
    case DepthRenderTargetFormat::kD24S8:
      return "kD24S8";
    case DepthRenderTargetFormat::kD24FS8:
      return "kD24FS8";
    default:
      return "kUnknown";
  }
}
static const char* const g_endian_names[] = {"none", "8 in 16", "8 in 32",
                                             "16 in 32"};

const char* GetEndianEnglishDescription(xenos::Endian endian) {
  return g_endian_names[static_cast<uint32_t>(endian)];
}
static const char* const g_primtype_human_names[] = {"none",
                                                     "point list",
                                                     "line list",
                                                     "line strip",
                                                     "triangle list",
                                                     "triangle fan",
                                                     "triangle strip",
                                                     "triangle with flags",
                                                     "rectangle list",
                                                     "unused1",
                                                     "unused2",
                                                     "unused3",
                                                     "line loop",
                                                     "quad list",
                                                     "quad strip",
                                                     "polygon",
                                                     "2D copy rect list v0",
                                                     "2D copy rect list v1",
                                                     "2D copy rect list v2",
                                                     "2D copy rect list v3",
                                                     "2D fillrect list",
                                                     "2D line strip",
                                                     "2D triangle strip"};

const char* GetPrimitiveTypeEnglishDescription(xenos::PrimitiveType prim_type) {
  return g_primtype_human_names[static_cast<uint32_t>(prim_type)];
}
}  // namespace xenos
}  // namespace gpu
}  // namespace xe
