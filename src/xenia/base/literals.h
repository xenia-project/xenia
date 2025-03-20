/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_LITERALS_H_
#define XENIA_BASE_LITERALS_H_

#include <cstdint>

namespace xe::literals {

constexpr size_t operator""_KiB(const unsigned long long int x) {
  return 1024ULL * x;
}

constexpr size_t operator""_MiB(const unsigned long long int x) {
  return 1024_KiB * x;
}

constexpr size_t operator""_GiB(const unsigned long long int x) {
  return 1024_MiB * x;
}

constexpr size_t operator""_TiB(const unsigned long long int x) {
  return 1024_GiB * x;
}

constexpr size_t operator""_PiB(const unsigned long long int x) {
  return 1024_TiB * x;
}

}  // namespace xe::literals

#endif  // XENIA_BASE_LITERALS_H_
