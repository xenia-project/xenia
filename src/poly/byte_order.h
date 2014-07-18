/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef POLY_BYTE_ORDER_H_
#define POLY_BYTE_ORDER_H_

#include <cstdint>

#include <poly/config.h>
#include <poly/platform.h>

#if XE_LIKE_OSX
#include <libkern/OSByteOrder.h>
#endif  // XE_LIKE_OSX

namespace poly {

#if XE_COMPILER_MSVC
#define POLY_BYTE_SWAP_16 _byteswap_ushort
#define POLY_BYTE_SWAP_32 _byteswap_ulong
#define POLY_BYTE_SWAP_64 _byteswap_uint64
#elif XE_LIKE_OSX
#define POLY_BYTE_SWAP_16 OSSwapInt16
#define POLY_BYTE_SWAP_32 OSSwapInt32
#define POLY_BYTE_SWAP_64 OSSwapInt64
#else
#define POLY_BYTE_SWAP_16 __bswap_16
#define POLY_BYTE_SWAP_32 __bswap_32
#define POLY_BYTE_SWAP_64 __bswap_64
#endif  // XE_COMPILER_MSVC

inline int16_t byte_swap(int16_t value) {
  return static_cast<int16_t>(POLY_BYTE_SWAP_16(static_cast<int16_t>(value)));
}
inline uint16_t byte_swap(uint16_t value) { return POLY_BYTE_SWAP_16(value); }
inline int32_t byte_swap(int32_t value) {
  return static_cast<int32_t>(POLY_BYTE_SWAP_32(static_cast<int32_t>(value)));
}
inline uint32_t byte_swap(uint32_t value) { return POLY_BYTE_SWAP_32(value); }
inline int64_t byte_swap(int64_t value) {
  return static_cast<int64_t>(POLY_BYTE_SWAP_64(static_cast<int64_t>(value)));
}
inline uint64_t byte_swap(uint64_t value) { return POLY_BYTE_SWAP_64(value); }
inline float byte_swap(float value) {
  uint32_t temp = byte_swap(*reinterpret_cast<uint32_t *>(&value));
  return *reinterpret_cast<float *>(&temp);
}
inline double byte_swap(double value) {
  uint64_t temp = byte_swap(*reinterpret_cast<uint64_t *>(&value));
  return *reinterpret_cast<double *>(&temp);
}

}  // namespace poly

#endif  // POLY_BYTE_ORDER_H_
