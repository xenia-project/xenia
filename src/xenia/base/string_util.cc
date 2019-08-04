/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/string_util.h"

#include <cinttypes>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "xenia/base/platform.h"
#include "xenia/base/vec128.h"

namespace xe {
namespace string_util {

std::string to_hex_string(uint32_t value) {
  char buffer[21];
  std::snprintf(buffer, sizeof(buffer), "%08" PRIX32, value);
  return std::string(buffer);
}

std::string to_hex_string(uint64_t value) {
  char buffer[21];
  std::snprintf(buffer, sizeof(buffer), "%016" PRIX64, value);
  return std::string(buffer);
}

std::string to_hex_string(const vec128_t& value) {
  char buffer[128];
  std::snprintf(buffer, sizeof(buffer), "[%.8X, %.8X, %.8X, %.8X]",
                value.u32[0], value.u32[1], value.u32[2], value.u32[3]);
  return std::string(buffer);
}

#if XE_ARCH_AMD64

// TODO(DrChat): This should not exist. Force the caller to use vec128.
std::string to_hex_string(const __m128& value) {
  char buffer[128];
  float f[4];
  _mm_storeu_ps(f, value);
  std::snprintf(
      buffer, sizeof(buffer), "[%.8X, %.8X, %.8X, %.8X]",
      *reinterpret_cast<uint32_t*>(&f[0]), *reinterpret_cast<uint32_t*>(&f[1]),
      *reinterpret_cast<uint32_t*>(&f[2]), *reinterpret_cast<uint32_t*>(&f[3]));
  return std::string(buffer);
}

std::string to_string(const __m128& value) {
  char buffer[128];
  float f[4];
  _mm_storeu_ps(f, value);
  std::snprintf(buffer, sizeof(buffer), "(%F, %F, %F, %F)", f[0], f[1], f[2],
                f[3]);
  return std::string(buffer);
}

#endif

}  // namespace string_util
}  // namespace xe
