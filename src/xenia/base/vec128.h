/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_VEC128_H_
#define XENIA_BASE_VEC128_H_

#include <cstddef>
#include <string>

#include "xenia/base/math.h"
#include "xenia/base/platform.h"

namespace xe {

// The first rule of vector programming is to only rely on exact positions
// when absolutely required - prefer dumb loops to exact offsets.
// Vectors in memory are laid out as in AVX registers on little endian
// machines. Note that little endian is dumb, so the byte at index 0 in
// the vector is is really byte 15 (or the high byte of short 7 or int 3).
// Because of this, all byte access should be via the accessors instead of
// the direct array.

// Altivec big endian layout:         AVX little endian layout:
// +---------+---------+---------+    +---------+---------+---------+
// | int32 0 | int16 0 | int8  0 |    | int32 3 | int16 7 | int8 15 |
// |         |         +---------+    |         |         +---------+
// |         |         | int8  1 |    |         |         | int8 14 |
// |         +---------+---------+    |         +---------+---------+
// |         | int16 1 | int8  2 |    |         | int16 6 | int8 13 |
// |         |         +---------+    |         |         +---------+
// |         |         | int8  3 |    |         |         | int8 12 |
// +---------+---------+---------+    +---------+---------+---------+
// | int32 1 | int16 2 | int8  4 |    | int32 2 | int16 5 | int8 11 |
// |         |         +---------+    |         |         +---------+
// |         |         | int8  5 |    |         |         | int8 10 |
// |         +---------+---------+    |         +---------+---------+
// |         | int16 3 | int8  6 |    |         | int16 4 | int8  9 |
// |         |         +---------+    |         |         +---------+
// |         |         | int8  7 |    |         |         | int8  8 |
// +---------+---------+---------+    +---------+---------+---------+
// | int32 2 | int16 4 | int8  8 |    | int32 1 | int16 3 | int8  7 |
// |         |         +---------+    |         |         +---------+
// |         |         | int8  9 |    |         |         | int8  6 |
// |         +---------+---------+    |         +---------+---------+
// |         | int16 5 | int8 10 |    |         | int16 2 | int8  5 |
// |         |         +---------+    |         |         +---------+
// |         |         | int8 11 |    |         |         | int8  4 |
// +---------+---------+---------+    +---------+---------+---------+
// | int32 3 | int16 6 | int8 12 |    | int32 0 | int16 1 | int8  3 |
// |         |         +---------+    |         |         +---------+
// |         |         | int8 13 |    |         |         | int8  2 |
// |         +---------+---------+    |         +---------+---------+
// |         | int16 7 | int8 14 |    |         | int16 0 | int8  1 |
// |         |         +---------+    |         |         +---------+
// |         |         | int8 15 |    |         |         | int8  0 |
// +---------+---------+---------+    +---------+---------+---------+
//
// Logical order:
// +-----+-----+-----+-----+          +-----+-----+-----+-----+
// |  X  |  Y  |  Z  |  W  |          |  W  |  Z  |  Y  |  X  |
// +-----+-----+-----+-----+          +-----+-----+-----+-----+
//
// Mapping indices is easy:
// int32[i ^ 0x3]
// int16[i ^ 0x7]
//  int8[i ^ 0xF]
typedef struct alignas(16) vec128_s {
  union {
    struct {
      float x;
      float y;
      float z;
      float w;
    };
    struct {
      int32_t ix;
      int32_t iy;
      int32_t iz;
      int32_t iw;
    };
    struct {
      uint32_t ux;
      uint32_t uy;
      uint32_t uz;
      uint32_t uw;
    };
    float f32[4];
    double f64[2];
    int8_t i8[16];
    uint8_t u8[16];
    int16_t i16[8];
    uint16_t u16[8];
    int32_t i32[4];
    uint32_t u32[4];
    int64_t i64[2];
    uint64_t u64[2];
    struct {
      uint64_t low;
      uint64_t high;
    };
  };

  vec128_s() = default;
  vec128_s(const vec128_s& other) {
    high = other.high;
    low = other.low;
  }

  vec128_s& operator=(const vec128_s& b) {
    high = b.high;
    low = b.low;
    return *this;
  }

  bool operator==(const vec128_s& b) const {
    return low == b.low && high == b.high;
  }
  bool operator!=(const vec128_s& b) const {
    return low != b.low || high != b.high;
  }
  vec128_s operator^(const vec128_s& b) const {
    vec128_s a = *this;
    a.high ^= b.high;
    a.low ^= b.low;
    return a;
  };
  vec128_s& operator^=(const vec128_s& b) {
    *this = *this ^ b;
    return *this;
  };
  vec128_s operator&(const vec128_s& b) const {
    vec128_s a = *this;
    a.high &= b.high;
    a.low &= b.low;
    return a;
  };
  vec128_s& operator&=(const vec128_s& b) {
    *this = *this & b;
    return *this;
  };
  vec128_s operator|(const vec128_s& b) const {
    vec128_s a = *this;
    a.high |= b.high;
    a.low |= b.low;
    return a;
  };
  vec128_s& operator|=(const vec128_s& b) {
    *this = *this | b;
    return *this;
  };
} vec128_t;

static inline vec128_t vec128i(uint32_t src) {
  vec128_t v;
  for (auto i = 0; i < 4; ++i) {
    v.u32[i] = src;
  }
  return v;
}
static inline vec128_t vec128i(uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
  vec128_t v;
  v.u32[0] = x;
  v.u32[1] = y;
  v.u32[2] = z;
  v.u32[3] = w;
  return v;
}
static inline vec128_t vec128q(uint64_t src) {
  vec128_t v;
  for (auto i = 0; i < 2; ++i) {
    v.i64[i] = src;
  }
  return v;
}
static inline vec128_t vec128q(uint64_t x, uint64_t y) {
  vec128_t v;
  v.i64[0] = x;
  v.i64[1] = y;
  return v;
}
static inline vec128_t vec128d(double src) {
  vec128_t v;
  for (auto i = 0; i < 2; ++i) {
    v.f64[i] = src;
  }
  return v;
}
static inline vec128_t vec128d(double x, double y) {
  vec128_t v;
  v.f64[0] = x;
  v.f64[1] = y;
  return v;
}
static inline vec128_t vec128f(float src) {
  vec128_t v;
  for (auto i = 0; i < 4; ++i) {
    v.f32[i] = src;
  }
  return v;
}
static inline vec128_t vec128f(float x, float y, float z, float w) {
  vec128_t v;
  v.f32[0] = x;
  v.f32[1] = y;
  v.f32[2] = z;
  v.f32[3] = w;
  return v;
}
static inline vec128_t vec128s(uint16_t src) {
  vec128_t v;
  for (auto i = 0; i < 8; ++i) {
    v.u16[i] = src;
  }
  return v;
}
static inline vec128_t vec128s(uint16_t x0, uint16_t x1, uint16_t y0,
                               uint16_t y1, uint16_t z0, uint16_t z1,
                               uint16_t w0, uint16_t w1) {
  vec128_t v;
  v.u16[0] = x1;
  v.u16[1] = x0;
  v.u16[2] = y1;
  v.u16[3] = y0;
  v.u16[4] = z1;
  v.u16[5] = z0;
  v.u16[6] = w1;
  v.u16[7] = w0;
  return v;
}
static inline vec128_t vec128b(uint8_t src) {
  vec128_t v;
  for (auto i = 0; i < 16; ++i) {
    v.u8[i] = src;
  }
  return v;
}
static inline vec128_t vec128b(uint8_t x0, uint8_t x1, uint8_t x2, uint8_t x3,
                               uint8_t y0, uint8_t y1, uint8_t y2, uint8_t y3,
                               uint8_t z0, uint8_t z1, uint8_t z2, uint8_t z3,
                               uint8_t w0, uint8_t w1, uint8_t w2, uint8_t w3) {
  vec128_t v;
  v.u8[0] = x3;
  v.u8[1] = x2;
  v.u8[2] = x1;
  v.u8[3] = x0;
  v.u8[4] = y3;
  v.u8[5] = y2;
  v.u8[6] = y1;
  v.u8[7] = y0;
  v.u8[8] = z3;
  v.u8[9] = z2;
  v.u8[10] = z1;
  v.u8[11] = z0;
  v.u8[12] = w3;
  v.u8[13] = w2;
  v.u8[14] = w1;
  v.u8[15] = w0;
  return v;
}

// TODO(gibbed): Figure out why clang doesn't line forward declarations of
// inline functions.

std::string to_string(const vec128_t& value);

}  // namespace xe

#endif  // XENIA_BASE_VEC128_H_
