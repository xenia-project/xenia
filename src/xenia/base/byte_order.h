/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_BYTE_ORDER_H_
#define XENIA_BASE_BYTE_ORDER_H_

#include <cstdint>

#include "xenia/base/assert.h"
#include "xenia/base/platform.h"

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
#define XENIA_BASE_BYTE_SWAP_16 __bswap_16
#define XENIA_BASE_BYTE_SWAP_32 __bswap_32
#define XENIA_BASE_BYTE_SWAP_64 __bswap_64
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
inline uint16_t byte_swap(wchar_t value) {
  return static_cast<wchar_t>(XENIA_BASE_BYTE_SWAP_16(value));
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

template <typename T>
struct be {
  be() = default;
  be(const T& src) : value(xe::byte_swap(src)) {}  // NOLINT(runtime/explicit)
  be(const be& other) { value = other.value; }     // NOLINT(runtime/explicit)
  operator T() const { return xe::byte_swap(value); }

  be<T>& operator+=(int a) {
    *this = *this + a;
    return *this;
  }
  be<T>& operator-=(int a) {
    *this = *this - a;
    return *this;
  }
  be<T>& operator++() {
    *this += 1;
    return *this;
  }  // ++a
  be<T> operator++(int) {
    *this += 1;
    return (*this - 1);
  }  // a++
  be<T>& operator--() {
    *this -= 1;
    return *this;
  }  // --a
  be<T> operator--(int) {
    *this -= 1;
    return (*this + 1);
  }  // a--

  T value;
};

}  // namespace xe

#endif  // XENIA_BASE_BYTE_ORDER_H_
