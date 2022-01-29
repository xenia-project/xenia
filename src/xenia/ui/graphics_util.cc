/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/graphics_util.h"

#include <cmath>

namespace xe {
namespace ui {

int32_t FloatToD3D11Fixed16p8(float f32) {
  // https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#3.2.4.1%20FLOAT%20-%3E%20Fixed%20Point%20Integer
  // Early exit tests.
  // n == NaN || n.unbiasedExponent < -f-1 -> 0 . 0
  if (!(std::abs(f32) >= 1.0f / 512.0f)) {
    return 0;
  }
  // n >= (2^(i-1)-2^-f) -> 2^(i-1)-1 . 2^f-1
  if (f32 >= 32768.0f - 1.0f / 256.0f) {
    return (1 << 23) - 1;
  }
  // n <= -2^(i-1) -> -2^(i-1) . 0
  if (f32 <= -32768.0f) {
    return -32768 * 256;
  }
  uint32_t f32_bits = *reinterpret_cast<const uint32_t*>(&f32);
  // Copy float32 mantissa bits [22:0] into corresponding bits [22:0] of a
  // result buffer that has at least 24 bits total storage (before reaching
  // rounding step further below). This includes one bit for the hidden 1.
  // Set bit [23] (float32 hidden bit).
  // Clear bits [31:24].
  union {
    int32_t s;
    uint32_t u;
  } result;
  result.u = (f32_bits & ((1 << 23) - 1)) | (1 << 23);
  // If the sign bit is set in the float32 number (negative), then take the 2's
  // component of the entire set of bits.
  if ((f32_bits >> 31) != 0) {
    result.s = -result.s;
  }
  // Final calculation: extraBits = (mantissa - f) - n.unbiasedExponent
  // (guaranteed to be >= 0).
  int32_t exponent = int32_t((f32_bits >> 23) & 255) - 127;
  uint32_t extra_bits = uint32_t(15 - exponent);
  if (extra_bits) {
    // Round the 32-bit value to a decimal that is extraBits to the left of
    // the LSB end, using nearest-even.
    result.u += (1 << (extra_bits - 1)) - 1 + ((result.u >> extra_bits) & 1);
    // Shift right by extraBits (sign extending).
    result.s >>= extra_bits;
  }
  return result.s;
}

}  // namespace ui
}  // namespace xe
