/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_MATH_H_
#define XENIA_BASE_MATH_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include "xenia/base/platform.h"

#if XE_ARCH_AMD64
#include <xmmintrin.h>
#endif

namespace xe {

template <typename T, size_t N>
size_t countof(T (&arr)[N]) {
  return std::extent<T[N]>::value;
}

// Rounds up the given value to the given alignment.
template <typename T>
T align(T value, T alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

// Rounds the given number up to the next highest multiple.
template <typename T, typename V>
T round_up(T value, V multiple) {
  return value ? (((value + multiple - 1) / multiple) * multiple) : multiple;
}

inline float saturate(float value) {
  return std::max(std::min(1.0f, value), -1.0f);
}

// Gets the next power of two value that is greater than or equal to the given
// value.
template <typename T>
T next_pow2(T value) {
  value--;
  value |= value >> 1;
  value |= value >> 2;
  value |= value >> 4;
  value |= value >> 8;
  value |= value >> 16;
  value++;
  return value;
}

constexpr uint32_t make_bitmask(uint32_t a, uint32_t b) {
  return (static_cast<uint32_t>(-1) >> (31 - b)) & ~((1u << a) - 1);
}

constexpr uint32_t select_bits(uint32_t value, uint32_t a, uint32_t b) {
  return (value & make_bitmask(a, b)) >> a;
}

inline uint32_t bit_count(uint32_t v) {
  v = v - ((v >> 1) & 0x55555555);
  v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
  return ((v + (v >> 4) & 0xF0F0F0F) * 0x1010101) >> 24;
}

inline uint32_t bit_count(uint64_t v) {
  v = (v & 0x5555555555555555LU) + (v >> 1 & 0x5555555555555555LU);
  v = (v & 0x3333333333333333LU) + (v >> 2 & 0x3333333333333333LU);
  v = v + (v >> 4) & 0x0F0F0F0F0F0F0F0FLU;
  v = v + (v >> 8);
  v = v + (v >> 16);
  v = v + (v >> 32) & 0x0000007F;
  return static_cast<uint32_t>(v);
}

// lzcnt instruction, typed for integers of all sizes.
// The number of leading zero bits in the value parameter. If value is zero, the
// return value is the size of the input operand (8, 16, 32, or 64). If the most
// significant bit of value is one, the return value is zero.
#if XE_PLATFORM_WIN32
// TODO(benvanik): runtime magic so these point to an appropriate implementation
// at runtime based on CPU features
#if 0
inline uint8_t lzcnt(uint8_t v) {
  return static_cast<uint8_t>(__lzcnt16(v) - 8);
}
inline uint8_t lzcnt(uint16_t v) { return static_cast<uint8_t>(__lzcnt16(v)); }
inline uint8_t lzcnt(uint32_t v) { return static_cast<uint8_t>(__lzcnt(v)); }
inline uint8_t lzcnt(uint64_t v) { return static_cast<uint8_t>(__lzcnt64(v)); }
#else
inline uint8_t lzcnt(uint8_t v) {
  unsigned long index;
  unsigned long mask = v;
  unsigned char is_nonzero = _BitScanReverse(&index, mask);
  return static_cast<uint8_t>(is_nonzero ? int8_t(index) ^ 0x7 : 8);
}
inline uint8_t lzcnt(uint16_t v) {
  unsigned long index;
  unsigned long mask = v;
  unsigned char is_nonzero = _BitScanReverse(&index, mask);
  return static_cast<uint8_t>(is_nonzero ? int8_t(index) ^ 0xF : 16);
}
inline uint8_t lzcnt(uint32_t v) {
  unsigned long index;
  unsigned long mask = v;
  unsigned char is_nonzero = _BitScanReverse(&index, mask);
  return static_cast<uint8_t>(is_nonzero ? int8_t(index) ^ 0x1F : 32);
}
inline uint8_t lzcnt(uint64_t v) {
  unsigned long index;
  unsigned long long mask = v;
  unsigned char is_nonzero = _BitScanReverse64(&index, mask);
  return static_cast<uint8_t>(is_nonzero ? int8_t(index) ^ 0x3F : 64);
}
#endif  // LZCNT supported

inline uint8_t tzcnt(uint8_t v) {
  unsigned long index;
  unsigned long mask = v;
  unsigned char is_nonzero = _BitScanForward(&index, mask);
  return static_cast<uint8_t>(is_nonzero ? int8_t(index) ^ 0x7 : 8);
}

inline uint8_t tzcnt(uint16_t v) {
  unsigned long index;
  unsigned long mask = v;
  unsigned char is_nonzero = _BitScanForward(&index, mask);
  return static_cast<uint8_t>(is_nonzero ? int8_t(index) ^ 0xF : 16);
}

inline uint8_t tzcnt(uint32_t v) {
  unsigned long index;
  unsigned long mask = v;
  unsigned char is_nonzero = _BitScanForward(&index, mask);
  return static_cast<uint8_t>(is_nonzero ? int8_t(index) ^ 0x1F : 32);
}

inline uint8_t tzcnt(uint64_t v) {
  unsigned long index;
  unsigned long long mask = v;
  unsigned char is_nonzero = _BitScanForward64(&index, mask);
  return static_cast<uint8_t>(is_nonzero ? int8_t(index) ^ 0x3F : 64);
}

#else  // XE_PLATFORM_WIN32
inline uint8_t lzcnt(uint8_t v) {
  return v == 0 ? 8 : static_cast<uint8_t>(__builtin_clz(v) - 24);
}
inline uint8_t lzcnt(uint16_t v) {
  return v == 0 ? 16 : static_cast<uint8_t>(__builtin_clz(v) - 16);
}
inline uint8_t lzcnt(uint32_t v) {
  return v == 0 ? 32 : static_cast<uint8_t>(__builtin_clz(v));
}
inline uint8_t lzcnt(uint64_t v) {
  return v == 0 ? 64 : static_cast<uint8_t>(__builtin_clzll(v));
}

inline uint8_t tzcnt(uint8_t v) {
  return v == 0 ? 8 : static_cast<uint8_t>(__builtin_ctz(v));
}
inline uint8_t tzcnt(uint16_t v) {
  return v == 0 ? 16 : static_cast<uint8_t>(__builtin_ctz(v));
}
inline uint8_t tzcnt(uint32_t v) {
  return v == 0 ? 32 : static_cast<uint8_t>(__builtin_ctz(v));
}
inline uint8_t tzcnt(uint64_t v) {
  return v == 0 ? 64 : static_cast<uint8_t>(__builtin_ctzll(v));
}
#endif
inline uint8_t lzcnt(int8_t v) { return lzcnt(static_cast<uint8_t>(v)); }
inline uint8_t lzcnt(int16_t v) { return lzcnt(static_cast<uint16_t>(v)); }
inline uint8_t lzcnt(int32_t v) { return lzcnt(static_cast<uint32_t>(v)); }
inline uint8_t lzcnt(int64_t v) { return lzcnt(static_cast<uint64_t>(v)); }
inline uint8_t tzcnt(int8_t v) { return tzcnt(static_cast<uint8_t>(v)); }
inline uint8_t tzcnt(int16_t v) { return tzcnt(static_cast<uint16_t>(v)); }
inline uint8_t tzcnt(int32_t v) { return tzcnt(static_cast<uint32_t>(v)); }
inline uint8_t tzcnt(int64_t v) { return tzcnt(static_cast<uint64_t>(v)); }

// BitScanForward (bsf).
// Search the value from least significant bit (LSB) to the most significant bit
// (MSB) for a set bit (1).
// Returns false if no bits are set and the output index is invalid.
#if XE_PLATFORM_WIN32
inline bool bit_scan_forward(uint32_t v, uint32_t* out_first_set_index) {
  return _BitScanForward(reinterpret_cast<unsigned long*>(out_first_set_index),
                         v) != 0;
}
inline bool bit_scan_forward(uint64_t v, uint32_t* out_first_set_index) {
  return _BitScanForward64(
             reinterpret_cast<unsigned long*>(out_first_set_index), v) != 0;
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
#endif  // XE_PLATFORM_WIN32
inline bool bit_scan_forward(int32_t v, uint32_t* out_first_set_index) {
  return bit_scan_forward(static_cast<uint32_t>(v), out_first_set_index);
}
inline bool bit_scan_forward(int64_t v, uint32_t* out_first_set_index) {
  return bit_scan_forward(static_cast<uint64_t>(v), out_first_set_index);
}

template <typename T>
inline T log2_floor(T v) {
  return sizeof(T) * 8 - 1 - lzcnt(v);
}
template <typename T>
inline T log2_ceil(T v) {
  return sizeof(T) * 8 - lzcnt(v - 1);
}

template <typename T>
inline T rotate_left(T v, uint8_t sh) {
  return (T(v) << sh) | (T(v) >> ((sizeof(T) * 8) - sh));
}
#if XE_PLATFORM_WIN32
template <>
inline uint8_t rotate_left(uint8_t v, uint8_t sh) {
  return _rotl8(v, sh);
}
template <>
inline uint16_t rotate_left(uint16_t v, uint8_t sh) {
  return _rotl16(v, sh);
}
template <>
inline uint32_t rotate_left(uint32_t v, uint8_t sh) {
  return _rotl(v, sh);
}
template <>
inline uint64_t rotate_left(uint64_t v, uint8_t sh) {
  return _rotl64(v, sh);
}
#endif  // XE_PLATFORM_WIN32

template <typename T>
T clamp(T value, T min_value, T max_value) {
  const T t = value < min_value ? min_value : value;
  return t > max_value ? max_value : t;
}

#if XE_ARCH_AMD64
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
#endif

uint16_t float_to_half(float value);
float half_to_float(uint16_t value);

}  // namespace xe

#endif  // XENIA_BASE_MATH_H_
