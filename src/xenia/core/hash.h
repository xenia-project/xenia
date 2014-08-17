/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CORE_HASH_H_
#define XENIA_CORE_HASH_H_

#include <xenia/common.h>

namespace xe {

inline size_t hash_combine(size_t seed) { return seed; }

template <typename T, typename... Ts>
size_t hash_combine(size_t seed, const T& v, const Ts&... vs) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9E3779B9 + (seed << 6) + (seed >> 2);
  return hash_combine(seed, vs...);
}

uint64_t hash64(const void* data, size_t length, uint64_t seed = 0);

}  // namespace xe

#endif  // XENIA_CORE_HASH_H_
