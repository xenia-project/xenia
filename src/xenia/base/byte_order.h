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

// chrispy: added workaround for clang, otherwise byteswap_ulong becomes calls
// to ucrtbase
#if XE_COMPILER_MSVC == 1 && !defined(__clang__)
#define XENIA_BASE_BYTE_SWAP_16 _byteswap_ushort
#define XENIA_BASE_BYTE_SWAP_32 _byteswap_ulong
#define XENIA_BASE_BYTE_SWAP_64 _byteswap_uint64
#else
#define XENIA_BASE_BYTE_SWAP_16 __builtin_bswap16
#define XENIA_BASE_BYTE_SWAP_32 __builtin_bswap32
#define XENIA_BASE_BYTE_SWAP_64 __builtin_bswap64
#endif  // XE_PLATFORM_WIN32

template <class T>
inline T byte_swap(T value) {
  static_assert(
      sizeof(T) == 8 || sizeof(T) == 4 || sizeof(T) == 2 || sizeof(T) == 1,
      "byte_swap(T value): Type T has illegal size");
  if constexpr (sizeof(T) == 8) {
    uint64_t temp =
        XENIA_BASE_BYTE_SWAP_64(*reinterpret_cast<uint64_t*>(&value));
    return *reinterpret_cast<T*>(&temp);
  } else if constexpr (sizeof(T) == 4) {
    uint32_t temp =
        XENIA_BASE_BYTE_SWAP_32(*reinterpret_cast<uint32_t*>(&value));
    return *reinterpret_cast<T*>(&temp);
  } else if constexpr (sizeof(T) == 2) {
    uint16_t temp =
        XENIA_BASE_BYTE_SWAP_16(*reinterpret_cast<uint16_t*>(&value));
    return *reinterpret_cast<T*>(&temp);
  } else if constexpr (sizeof(T) == 1) {
    return value;
  }
}

template <typename T, std::endian E>
struct endian_store {
  endian_store() = default;
  endian_store(const T& src) { set(src); }
  endian_store(const endian_store& other) { set(other); }
  operator T() const { return get(); }

  void set(const T& src) {
    if constexpr (std::endian::native == E) {
      value = src;
    } else {
      value = xe::byte_swap(src);
    }
  }
  void set(const endian_store& other) { value = other.value; }
  T get() const {
    if constexpr (std::endian::native == E) {
      return value;
    }
    return xe::byte_swap(value);
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
