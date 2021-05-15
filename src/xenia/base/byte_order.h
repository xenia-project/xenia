/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_BYTE_ORDER_H_
#define XENIA_BASE_BYTE_ORDER_H_

#include <cstdint>
#if defined __has_include
#if __has_include(<version>)
#include <version>
#endif
#endif
#if __cpp_lib_endian
#include <bit>
#endif

#include "xenia/base/assert.h"
#include "xenia/base/platform.h"

#if XE_PLATFORM_LINUX
#include <byteswap.h>
#endif

#if !__cpp_lib_endian
// Polyfill
#ifdef __BYTE_ORDER__
namespace std {
enum class endian {
  little = __ORDER_LITTLE_ENDIAN__,
  big = __ORDER_BIG_ENDIAN__,
  native = __BYTE_ORDER__
};
}
#else
// Hardcode to little endian for now
namespace std {
enum class endian { little = 0, big = 1, native = 0 };
}
#endif
#endif
// Check for mixed endian
static_assert((std::endian::native == std::endian::big) ||
              (std::endian::native == std::endian::little));

namespace xe {

#if XE_PLATFORM_WIN32
#define XENIA_BASE_BYTE_SWAP_16 _byteswap_ushort
#define XENIA_BASE_BYTE_SWAP_32 _byteswap_ulong
#define XENIA_BASE_BYTE_SWAP_64 _byteswap_uint64
#elif XE_PLATFORM_MAC
#define XENIA_BASE_BYTE_SWAP_16 OSSwapInt16
#define XENIA_BASE_BYTE_SWAP_32 OSSwapInt32
#define XENIA_BASE_BYTE_SWAP_64 OSSwapInt64
#else
#define XENIA_BASE_BYTE_SWAP_16 bswap_16
#define XENIA_BASE_BYTE_SWAP_32 bswap_32
#define XENIA_BASE_BYTE_SWAP_64 bswap_64
#endif  // XE_PLATFORM_WIN32

inline int8_t byte_swap(int8_t value) { return value; }
inline uint8_t byte_swap(uint8_t value) { return value; }
inline int16_t byte_swap(int16_t value) {
  return static_cast<int16_t>(
      XENIA_BASE_BYTE_SWAP_16(static_cast<int16_t>(value)));
}
inline uint16_t byte_swap(uint16_t value) {
  return XENIA_BASE_BYTE_SWAP_16(value);
}
inline uint16_t byte_swap(char16_t value) {
  return static_cast<char16_t>(XENIA_BASE_BYTE_SWAP_16(value));
}
inline int32_t byte_swap(int32_t value) {
  return static_cast<int32_t>(
      XENIA_BASE_BYTE_SWAP_32(static_cast<int32_t>(value)));
}
inline uint32_t byte_swap(uint32_t value) {
  return XENIA_BASE_BYTE_SWAP_32(value);
}
inline int64_t byte_swap(int64_t value) {
  return static_cast<int64_t>(
      XENIA_BASE_BYTE_SWAP_64(static_cast<int64_t>(value)));
}
inline uint64_t byte_swap(uint64_t value) {
  return XENIA_BASE_BYTE_SWAP_64(value);
}
inline float byte_swap(float value) {
  uint32_t temp = byte_swap(*reinterpret_cast<uint32_t*>(&value));
  return *reinterpret_cast<float*>(&temp);
}
inline double byte_swap(double value) {
  uint64_t temp = byte_swap(*reinterpret_cast<uint64_t*>(&value));
  return *reinterpret_cast<double*>(&temp);
}
template <typename T>
inline T byte_swap(T value) {
  if (sizeof(T) == 4) {
    return static_cast<T>(byte_swap(static_cast<uint32_t>(value)));
  } else if (sizeof(T) == 2) {
    return static_cast<T>(byte_swap(static_cast<uint16_t>(value)));
  } else {
    assert_always("not handled");
  }
}

template <typename T, std::endian E>
struct endian_store {
  endian_store() = default;
  endian_store(const T& src) {
    if constexpr (std::endian::native == E) {
      value = src;
    } else {
      value = xe::byte_swap(src);
    }
  }
  endian_store(const endian_store& other) { value = other.value; }
  operator T() const {
    if constexpr (std::endian::native == E) {
      return value;
    } else {
      return xe::byte_swap(value);
    }
  }

  endian_store<T, E>& operator+=(int a) {
    *this = *this + a;
    return *this;
  }
  endian_store<T, E>& operator-=(int a) {
    *this = *this - a;
    return *this;
  }
  endian_store<T, E>& operator++() {
    *this += 1;
    return *this;
  }  // ++a
  endian_store<T, E> operator++(int) {
    *this += 1;
    return (*this - 1);
  }  // a++
  endian_store<T, E>& operator--() {
    *this -= 1;
    return *this;
  }  // --a
  endian_store<T, E> operator--(int) {
    *this -= 1;
    return (*this + 1);
  }  // a--

  T value;
};

template <typename T>
using be = endian_store<T, std::endian::big>;
template <typename T>
using le = endian_store<T, std::endian::little>;

}  // namespace xe

#endif  // XENIA_BASE_BYTE_ORDER_H_
