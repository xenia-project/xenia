/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_VEC128_H_
#define ALLOY_VEC128_H_

#include <alloy/core.h>

namespace alloy {

typedef struct XECACHEALIGN vec128_s {
  union {
    struct {
      float x;
      float y;
      float z;
      float w;
    };
    struct {
      uint32_t ix;
      uint32_t iy;
      uint32_t iz;
      uint32_t iw;
    };
    float f4[4];
    uint32_t i4[4];
    uint16_t s8[8];
    uint8_t b16[16];
    struct {
      uint64_t low;
      uint64_t high;
    };
  };

  bool operator==(const vec128_s& b) const {
    return low == b.low && high == b.high;
  }
} vec128_t;
XEFORCEINLINE vec128_t vec128i(uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
  vec128_t v;
  v.i4[0] = x;
  v.i4[1] = y;
  v.i4[2] = z;
  v.i4[3] = w;
  return v;
}
XEFORCEINLINE vec128_t vec128f(float x, float y, float z, float w) {
  vec128_t v;
  v.f4[0] = x;
  v.f4[1] = y;
  v.f4[2] = z;
  v.f4[3] = w;
  return v;
}
XEFORCEINLINE vec128_t vec128b(uint8_t x0, uint8_t x1, uint8_t x2, uint8_t x3,
                               uint8_t y0, uint8_t y1, uint8_t y2, uint8_t y3,
                               uint8_t z0, uint8_t z1, uint8_t z2, uint8_t z3,
                               uint8_t w0, uint8_t w1, uint8_t w2, uint8_t w3) {
  vec128_t v;
  v.b16[0] = x3;
  v.b16[1] = x2;
  v.b16[2] = x1;
  v.b16[3] = x0;
  v.b16[4] = y3;
  v.b16[5] = y2;
  v.b16[6] = y1;
  v.b16[7] = y0;
  v.b16[8] = z3;
  v.b16[9] = z2;
  v.b16[10] = z1;
  v.b16[11] = z0;
  v.b16[12] = w3;
  v.b16[13] = w2;
  v.b16[14] = w1;
  v.b16[15] = w0;
  return v;
}

}  // namespace alloy

#endif  // ALLOY_VEC128_H_
