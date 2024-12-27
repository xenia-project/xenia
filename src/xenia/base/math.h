/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_MATH_H_
#define XENIA_BASE_MATH_H_

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>
#include <type_traits>

#if defined __has_include
#if __has_include(<version>)
#include <version>
#endif
#endif
#if __cpp_lib_bitops
#include <bit>
#endif

#include "xenia/base/platform.h"

#if XE_ARCH_AMD64
#include <xmmintrin.h>
#endif

namespace xe {

template <typename T, size_t N>
constexpr size_t countof(T (&arr)[N]) {
  return std::extent<T[N]>::value;
}

template <typename T>
constexpr bool is_pow2(T value) {
  return (value & (value - 1)) == 0;
}
/*
        Use this in place of the shift + and not sequence that is being used
   currently in bit iteration code. This is more efficient because it does not
   introduce a dependency on to the previous bit scanning operation. The shift
   and not sequence does get translated to a single instruction (the bit test
   and reset instruction), but this code can be executed alongside the scan
*/
template <typename T>
constexpr T clear_lowest_bit(T value) {
  static_assert(std::is_integral_v<T>);

  return (value - static_cast<T>(1)) & value;
}
// Rounds up the given value to the given alignment.
template <typename T>
constexpr T align(T value, T alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

// Rounds the given number up to the next highest multiple.
template <typename T, typename V>
constexpr T round_up(T value, V multiple, bool force_non_zero = true) {
  if (force_non_zero && !value) {
    return multiple;
  }
  return (value + multiple - 1) / multiple * multiple;
}

// For NaN, returns min_value (or, if it's NaN too, max_value).
// If either of the boundaries is zero, and if the value is at that boundary or
// exceeds it, the result will have the sign of that boundary. If both
// boundaries are zero, which sign is selected among the argument signs is not
// explicitly defined.
template <typename T>
T clamp_float(T value, T min_value, T max_value) {
  float clamped_to_min = std::isgreater(value, min_value) ? value : min_value;
  return std::isless(clamped_to_min, max_value) ? clamped_to_min : max_value;
}

// Using the same conventions as in shading languages, returning 0 for NaN.
// 0 is always returned as positive.
template <typename T>
T saturate(T value) {
  return clamp_float(value, static_cast<T>(0.0f), static_cast<T>(1.0f));
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

#if __cpp_lib_gcd_lcm
template <typename T>
constexpr T greatest_common_divisor(T a, T b) {
  return std::gcd(a, b);
}
#else
template <typename T>
constexpr T greatest_common_divisor(T a, T b) {
  // Use the Euclid algorithm to calculate the greatest common divisor
  while (b) {
    a = std::exchange(b, a % b);
  }
  return a;
}
#endif

template <typename T>
constexpr void reduce_fraction(T& numerator, T& denominator) {
  auto gcd = greatest_common_divisor(numerator, denominator);
  numerator /= gcd;
  denominator /= gcd;
}

template <typename T>
constexpr void reduce_fraction(std::pair<T, T>& fraction) {
  reduce_fraction<T>(fraction.first, fraction.second);
}

constexpr uint32_t make_bitmask(uint32_t a, uint32_t b) {
  return (static_cast<uint32_t>(-1) >> (31 - b)) & ~((1u << a) - 1);
}

constexpr uint32_t select_bits(uint32_t value, uint32_t a, uint32_t b) {
  return (value & make_bitmask(a, b)) >> a;
}

#if __cpp_lib_bitops
template <class T>
constexpr inline uint32_t bit_count(T v) {
  return static_cast<uint32_t>(std::popcount(v));
}
#else
#if XE_COMPILER_MSVC || XE_COMPILER_INTEL
inline uint32_t bit_count(uint32_t v) { return __popcnt(v); }
inline uint32_t bit_count(uint64_t v) {
  return static_cast<uint32_t>(__popcnt64(v));
}
#elif XE_COMPILER_GCC || XE_COMPILER_CLANG
static_assert(sizeof(unsigned int) == sizeof(uint32_t));
static_assert(sizeof(unsigned long long) == sizeof(uint64_t));
inline uint32_t bit_count(uint32_t v) { return __builtin_popcount(v); }
inline uint32_t bit_count(uint64_t v) { return __builtin_popcountll(v); }
#else
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
#endif
#endif

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
  return static_cast<uint8_t>(is_nonzero ? int8_t(index) : 8);
}

inline uint8_t tzcnt(uint16_t v) {
  unsigned long index;
  unsigned long mask = v;
  unsigned char is_nonzero = _BitScanForward(&index, mask);
  return static_cast<uint8_t>(is_nonzero ? int8_t(index) : 16);
}

inline uint8_t tzcnt(uint32_t v) {
  unsigned long index;
  unsigned long mask = v;
  unsigned char is_nonzero = _BitScanForward(&index, mask);
  return static_cast<uint8_t>(is_nonzero ? int8_t(index) : 32);
}

inline uint8_t tzcnt(uint64_t v) {
  unsigned long index;
  unsigned long long mask = v;
  unsigned char is_nonzero = _BitScanForward64(&index, mask);
  return static_cast<uint8_t>(is_nonzero ? int8_t(index) : 64);
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
  *out_first_set_index = i - 1;
  return i != 0;
}
inline bool bit_scan_forward(uint64_t v, uint32_t* out_first_set_index) {
  int i = __builtin_ffsll(v);
  *out_first_set_index = i - 1;
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
  return (T(v) << sh) | (T(v) >> ((sizeof(T) * CHAR_BIT) - sh));
}
template <typename T>
inline T rotate_right(T v, uint8_t sh) {
  constexpr unsigned char SHIFT_MASK = (CHAR_BIT * sizeof(T)) - 1;
  uint8_t rshr = sh & SHIFT_MASK;
  uint8_t lshl = static_cast<uint8_t>(-static_cast<int8_t>(sh)) & SHIFT_MASK;
  return (v >> rshr) | (v << lshl);
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
template <>
inline uint8_t rotate_right(uint8_t v, uint8_t sh) {
  return _rotr8(v, sh);
}
template <>
inline uint16_t rotate_right(uint16_t v, uint8_t sh) {
  return _rotr16(v, sh);
}
template <>
inline uint32_t rotate_right(uint32_t v, uint8_t sh) {
  return _rotr(v, sh);
}
template <>
inline uint64_t rotate_right(uint64_t v, uint8_t sh) {
  return _rotr64(v, sh);
}
#endif  // XE_PLATFORM_WIN32

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
/*

        std::min/max float has handling for nans, where if either argument is
   nan the first argument is returned

        minss/maxss are different, if either argument is nan the second operand
   to the instruction is returned this is problematic because we have no
   assurances from the compiler on the argument ordering

        so only use in places where nan handling is not needed
*/
XE_FORCEINLINE
static float ArchMin(float x, float y) {
  return _mm_cvtss_f32(_mm_min_ss(_mm_set_ss(x), _mm_set_ss(y)));
}
XE_FORCEINLINE
static float ArchMax(float x, float y) {
  return _mm_cvtss_f32(_mm_max_ss(_mm_set_ss(x), _mm_set_ss(y)));
}
XE_FORCEINLINE
static float ArchReciprocal(float den) {
  return _mm_cvtss_f32(_mm_rcp_ss(_mm_set_ss(den)));
}

using ArchFloatMask = __m128;

XE_FORCEINLINE
static ArchFloatMask ArchCmpneqFloatMask(float x, float y) {
  return _mm_cmpneq_ss(_mm_set_ss(x), _mm_set_ss(y));
}
XE_FORCEINLINE
static ArchFloatMask ArchORFloatMask(ArchFloatMask x, ArchFloatMask y) {
  return _mm_or_ps(x, y);
}
XE_FORCEINLINE
static ArchFloatMask ArchXORFloatMask(ArchFloatMask x, ArchFloatMask y) {
  return _mm_xor_ps(x, y);
}

XE_FORCEINLINE
static ArchFloatMask ArchANDFloatMask(ArchFloatMask x, ArchFloatMask y) {
  return _mm_and_ps(x, y);
}

XE_FORCEINLINE
static uint32_t ArchFloatMaskSignbit(ArchFloatMask x) {
  return static_cast<uint32_t>(_mm_movemask_ps(x) & 1);
}

constexpr ArchFloatMask floatmask_zero{.0f};

#else
static float ArchMin(float x, float y) { return std::min<float>(x, y); }
static float ArchMax(float x, float y) { return std::max<float>(x, y); }
static float ArchReciprocal(float den) { return 1.0f / den; }
using ArchFloatMask = unsigned;

XE_FORCEINLINE
static ArchFloatMask ArchCmpneqFloatMask(float x, float y) {
  return static_cast<unsigned>(-static_cast<signed>(x != y));
}

XE_FORCEINLINE
static ArchFloatMask ArchORFloatMask(ArchFloatMask x, ArchFloatMask y) {
  return x | y;
}
XE_FORCEINLINE
static ArchFloatMask ArchXORFloatMask(ArchFloatMask x, ArchFloatMask y) {
  return x ^ y;
}

XE_FORCEINLINE
static ArchFloatMask ArchANDFloatMask(ArchFloatMask x, ArchFloatMask y) {
  return x & y;
}
constexpr ArchFloatMask floatmask_zero = 0;

XE_FORCEINLINE
static uint32_t ArchFloatMaskSignbit(ArchFloatMask x) { return x >> 31; }

#endif
XE_FORCEINLINE
static float RefineReciprocal(float initial, float den) {
  float t0 = initial * den;
  float t1 = t0 * initial;
  float rcp2 = initial + initial;
  return rcp2 - t1;
}
XE_FORCEINLINE
static float ArchReciprocalRefined(float den) {
  return RefineReciprocal(ArchReciprocal(den), den);
}

// Similar to the C++ implementation of XMConvertFloatToHalf and
// XMConvertHalfToFloat from DirectXMath 3.00 (pre-3.04, which switched from the
// Xenos encoding to IEEE 754), with the extended range instead of infinity and
// NaN, and optionally with denormalized numbers - as used in vpkd3d128 (no
// denormals, rounding towards zero) and on the Xenos (GL_OES_texture_float
// alternative encoding).

inline uint16_t float_to_xenos_half(float value, bool preserve_denormal = false,
                                    bool round_to_nearest_even = false) {
  uint32_t integer_value = *reinterpret_cast<const uint32_t*>(&value);
  uint32_t abs_value = integer_value & 0x7FFFFFFFu;
  uint32_t result;
  if (abs_value >= 0x47FFE000u) {
    // Saturate.
    result = 0x7FFFu;
  } else {
    if (abs_value < 0x38800000u) {
      // The number is too small to be represented as a normalized half.
      if (preserve_denormal) {
        uint32_t shift =
            std::min(uint32_t(113u - (abs_value >> 23u)), uint32_t(24u));
        result = (0x800000u | (abs_value & 0x7FFFFFu)) >> shift;
      } else {
        result = 0u;
      }
    } else {
      // Rebias the exponent to represent the value as a normalized half.
      result = abs_value + 0xC8000000u;
    }
    if (round_to_nearest_even) {
      result += 0xFFFu + ((result >> 13u) & 1u);
    }
    result = (result >> 13u) & 0x7FFFu;
  }
  return uint16_t(result | ((integer_value & 0x80000000u) >> 16u));
}

inline float xenos_half_to_float(uint16_t value,
                                 bool preserve_denormal = false) {
  uint32_t mantissa = value & 0x3FFu;
  uint32_t exponent = (value >> 10u) & 0x1Fu;
  if (!exponent) {
    if (!preserve_denormal) {
      mantissa = 0;
    } else if (mantissa) {
      // Normalize the value in the resulting float.
      // do { Exponent--; Mantissa <<= 1; } while ((Mantissa & 0x0400) == 0)
      uint32_t mantissa_lzcnt = xe::lzcnt(mantissa) - (32u - 11u);
      exponent = uint32_t(1 - int32_t(mantissa_lzcnt));
      mantissa = (mantissa << mantissa_lzcnt) & 0x3FFu;
    }
    if (!mantissa) {
      exponent = uint32_t(-112);
    }
  }
  uint32_t result = (uint32_t(value & 0x8000u) << 16u) |
                    ((exponent + 112u) << 23u) | (mantissa << 13u);
  return *reinterpret_cast<const float*>(&result);
}

// https://locklessinc.com/articles/sat_arithmetic/
template <typename T>
inline T sat_add(T a, T b) {
  using TU = typename std::make_unsigned<T>::type;
  TU result = TU(a) + TU(b);
  if (std::is_unsigned<T>::value) {
    result |=
        TU(-static_cast<typename std::make_signed<T>::type>(result < TU(a)));
  } else {
    TU overflowed =
        (TU(a) >> (sizeof(T) * 8 - 1)) + std::numeric_limits<T>::max();
    if (T((overflowed ^ TU(b)) | ~(TU(b) ^ result)) >= 0) {
      result = overflowed;
    }
  }
  return T(result);
}
template <typename T>
inline T sat_sub(T a, T b) {
  using TU = typename std::make_unsigned<T>::type;
  TU result = TU(a) - TU(b);
  if (std::is_unsigned<T>::value) {
    result &=
        TU(-static_cast<typename std::make_signed<T>::type>(result <= TU(a)));
  } else {
    TU overflowed =
        (TU(a) >> (sizeof(T) * 8 - 1)) + std::numeric_limits<T>::max();
    if (T((overflowed ^ TU(b)) & (overflowed ^ result)) < 0) {
      result = overflowed;
    }
  }
  return T(result);
}

template <typename T>
inline T roundToNearestOrderOfMagnitude(T value) {
  if (!value) {
    return value;
  }

  const double order = std::pow(10, std::floor(std::log10(std::fabs(value))));
  const double rounded = std::round(value / order) * order;

  return static_cast<T>(rounded);
}

namespace divisors {
union IDivExtraInfo {
  uint32_t value_;
  struct {
    uint32_t shift_ : 31;
    uint32_t add_ : 1;
  } info;
};
// returns magicnum multiplier
static constexpr uint32_t PregenerateUint32Div(uint32_t _denom,
                                               uint32_t& out_extra) {
  IDivExtraInfo extra{};

  uint32_t d = _denom;
  int p = 0;
  uint32_t nc = 0, delta = 0, q1 = 0, r1 = 0, q2 = 0, r2 = 0;
  struct {
    unsigned M;
    int a;
    int s;
  } magu{};
  magu.a = 0;
  nc = -1 - ((uint32_t) - (int32_t)d) % d;
  p = 31;
  q1 = 0x80000000 / nc;
  r1 = 0x80000000 - q1 * nc;
  q2 = 0x7FFFFFFF / d;
  r2 = 0x7FFFFFFF - q2 * d;
  do {
    p += 1;
    if (r1 >= nc - r1) {
      q1 = 2 * q1 + 1;
      r1 = 2 * r1 - nc;
    } else {
      q1 = 2 * q1;
      r1 = 2 * r1;
    }
    if (r2 + 1 >= d - r2) {
      if (q2 >= 0x7FFFFFFF) {
        magu.a = 1;
      }
      q2 = 2 * q2 + 1;
      r2 = 2 * r2 + 1 - d;

    } else {
      if (q2 >= 0x80000000U) {
        magu.a = 1;
      }
      q2 = 2 * q2;
      r2 = 2 * r2 + 1;
    }
    delta = d - 1 - r2;
  } while (p < 64 && (q1 < delta || r1 == 0));

  extra.info.add_ = magu.a;
  extra.info.shift_ = p - 32;
  out_extra = extra.value_;
  return static_cast<uint64_t>(q2 + 1);
}

static constexpr uint32_t ApplyUint32Div(uint32_t num, uint32_t mul,
                                         uint32_t extradata) {
  IDivExtraInfo extra{};

  extra.value_ = extradata;

  uint32_t result = static_cast<uint32_t>(
      (static_cast<uint64_t>(num) * static_cast<uint64_t>(mul)) >> 32);
  if (extra.info.add_) {
    uint32_t addend = result + num;
    addend = ((addend < result ? 0x80000000 : 0) | addend);
    result = addend;
  }
  return result >> extra.info.shift_;
}

static constexpr uint32_t ApplyUint32UMod(uint32_t num, uint32_t mul,
                                          uint32_t extradata,
                                          uint32_t original) {
  uint32_t dived = ApplyUint32Div(num, mul, extradata);
  unsigned result = num - (dived * original);

  return result;
}

struct MagicDiv {
  uint32_t multiplier_;
  uint32_t extradata_;
  constexpr MagicDiv() : multiplier_(0), extradata_(0) {}
  constexpr MagicDiv(uint32_t original) : MagicDiv() {
    multiplier_ = PregenerateUint32Div(original, extradata_);
  }

  constexpr uint32_t GetRightShift() const {
    IDivExtraInfo extra{};

    extra.value_ = extradata_;
    return extra.info.shift_;
  }

  constexpr bool AddFlag() const {
    IDivExtraInfo extra{};

    extra.value_ = extradata_;
    return extra.info.shift_;
  }

  constexpr uint32_t GetMultiplier() const { return multiplier_; }
  constexpr uint32_t Apply(uint32_t numerator) const {
    return ApplyUint32Div(numerator, multiplier_, extradata_);
  }
};
}  // namespace divisors
}  // namespace xe

#endif  // XENIA_BASE_MATH_H_
