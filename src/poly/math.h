/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_MATH_H_
#define POLY_MATH_H_

#include <xmmintrin.h>

#include <cstdint>
#include <cstring>

#include <poly/config.h>
#include <poly/platform.h>

namespace poly {

// lzcnt instruction, typed for integers of all sizes.
// The number of leading zero bits in the value parameter. If value is zero, the
// return value is the size of the input operand (8, 16, 32, or 64). If the most
// significant bit of value is one, the return value is zero.
#if XE_COMPILER_MSVC
inline uint8_t lzcnt(uint8_t v) {
  return static_cast<uint8_t>(__lzcnt16(v) - 8);
}
inline uint8_t lzcnt(uint16_t v) { return static_cast<uint8_t>(__lzcnt16(v)); }
inline uint8_t lzcnt(uint32_t v) { return static_cast<uint8_t>(__lzcnt(v)); }
inline uint8_t lzcnt(uint64_t v) { return static_cast<uint8_t>(__lzcnt64(v)); }
#else
inline uint8_t lzcnt(uint8_t v) {
  return static_cast<uint8_t>(__builtin_clzs(v) - 8);
}
inline uint8_t lzcnt(uint16_t v) {
  return static_cast<uint8_t>(__builtin_clzs(v));
}
inline uint8_t lzcnt(uint32_t v) {
  return static_cast<uint8_t>(__builtin_clz(v));
}
inline uint8_t lzcnt(uint64_t v) {
  return static_cast<uint8_t>(__builtin_clzll(v));
}
#endif  // XE_COMPILER_MSVC
inline uint8_t lzcnt(int8_t v) { return lzcnt(static_cast<uint8_t>(v)); }
inline uint8_t lzcnt(int16_t v) { return lzcnt(static_cast<uint16_t>(v)); }
inline uint8_t lzcnt(int32_t v) { return lzcnt(static_cast<uint32_t>(v)); }
inline uint8_t lzcnt(int64_t v) { return lzcnt(static_cast<uint64_t>(v)); }

// BitScanForward (bsf).
// Search the value from least significant bit (LSB) to the most significant bit
// (MSB) for a set bit (1).
// Returns false if no bits are set and the output index is invalid.
#if XE_COMPILER_MSVC
inline bool bit_scan_forward(uint32_t v, uint32_t* out_first_set_index) {
  return _BitScanForward(reinterpret_cast<DWORD*>(out_first_set_index), v) != 0;
}
inline bool bit_scan_forward(uint64_t v, uint32_t* out_first_set_index) {
  return _BitScanForward64(reinterpret_cast<DWORD*>(out_first_set_index), v) !=
         0;
}
#else
inline bool bit_scan_forward(uint32_t v, uint32_t* out_first_set_index) {
  int i = ffs(v);
  *out_first_set_index = i;
  return i != 0;
}
inline bool bit_scan_forward(uint64_t v, uint32_t* out_first_set_index) {
  int i = ffsll(v);
  *out_first_set_index = i;
  return i != 0;
}
#endif  // XE_COMPILER_MSVC
inline bool bit_scan_forward(int32_t v, uint32_t* out_first_set_index) {
  return bit_scan_forward(static_cast<uint32_t>(v), out_first_set_index);
}
inline bool bit_scan_forward(int64_t v, uint32_t* out_first_set_index) {
  return bit_scan_forward(static_cast<uint64_t>(v), out_first_set_index);
}

// Utilities for SSE values.
template <int N>
float m128_f32(const __m128& v) {
  float ret;
  _mm_store_ss(&ret, _mm_shuffle_ps(v, v, _MM_SHUFFLE(N, N, N, N)));
  return ret;
}
template <int N>
int32_t m128_i32(const __m128& v) {
  union {
    float f;
    int32_t i;
  } ret;
  _mm_store_ss(&ret.f, _mm_shuffle_ps(v, v, _MM_SHUFFLE(N, N, N, N)));
  return ret.i;
}
template <int N>
double m128_f64(const __m128d& v) {
  double ret;
  _mm_store_sd(&ret, _mm_shuffle_pd(v, v, _MM_SHUFFLE2(N, N)));
  return ret;
}
template <int N>
double m128_f64(const __m128& v) {
  return m128_f64<N>(_mm_castps_pd(v));
}
template <int N>
int64_t m128_i64(const __m128d& v) {
  union {
    double f;
    int64_t i;
  } ret;
  _mm_store_sd(&ret.f, _mm_shuffle_pd(v, v, _MM_SHUFFLE2(N, N)));
  return ret.i;
}
template <int N>
int64_t m128_i64(const __m128& v) {
  return m128_i64<N>(_mm_castps_pd(v));
}

}  // namespace poly

#endif  // POLY_MATH_H_
