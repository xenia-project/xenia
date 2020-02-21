/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_STRING_UTIL_H_
#define XENIA_BASE_STRING_UTIL_H_

#include <cinttypes>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "xenia/base/platform.h"
#include "xenia/base/vec128.h"

namespace xe {
namespace string_util {

// TODO(gibbed): Figure out why clang doesn't line forward declarations of
// inline functions.

std::string to_hex_string(uint32_t value);
std::string to_hex_string(uint64_t value);

inline std::string to_hex_string(float value) {
  union {
    uint32_t ui;
    float flt;
  } v;
  v.flt = value;
  return to_hex_string(v.ui);
}

inline std::string to_hex_string(double value) {
  union {
    uint64_t ui;
    double dbl;
  } v;
  v.dbl = value;
  return to_hex_string(v.ui);
}

std::string to_hex_string(const vec128_t& value);

#if XE_ARCH_AMD64

// TODO(DrChat): This should not exist. Force the caller to use vec128.
std::string to_hex_string(const __m128& value);
std::string to_string(const __m128& value);

#endif

template <typename T>
inline T from_string(const char* value, bool force_hex = false) {
  // Missing implementation for converting type T to string
  throw;
}

template <>
inline bool from_string<bool>(const char* value, bool force_hex) {
  return std::strcmp(value, "true") == 0 || value[0] == '1';
}

template <>
inline int32_t from_string<int32_t>(const char* value, bool force_hex) {
  if (force_hex || std::strchr(value, 'h') != nullptr) {
    return std::strtol(value, nullptr, 16);
  } else {
    return std::strtol(value, nullptr, 0);
  }
}

template <>
inline uint32_t from_string<uint32_t>(const char* value, bool force_hex) {
  if (force_hex || std::strchr(value, 'h') != nullptr) {
    return std::strtoul(value, nullptr, 16);
  } else {
    return std::strtoul(value, nullptr, 0);
  }
}

template <>
inline int64_t from_string<int64_t>(const char* value, bool force_hex) {
  if (force_hex || std::strchr(value, 'h') != nullptr) {
    return std::strtoll(value, nullptr, 16);
  } else {
    return std::strtoll(value, nullptr, 0);
  }
}

template <>
inline uint64_t from_string<uint64_t>(const char* value, bool force_hex) {
  if (force_hex || std::strchr(value, 'h') != nullptr) {
    return std::strtoull(value, nullptr, 16);
  } else {
    return std::strtoull(value, nullptr, 0);
  }
}

template <>
inline float from_string<float>(const char* value, bool force_hex) {
  if (force_hex || std::strstr(value, "0x") == value ||
      std::strchr(value, 'h') != nullptr) {
    union {
      uint32_t ui;
      float flt;
    } v;
    v.ui = from_string<uint32_t>(value, force_hex);
    return v.flt;
  }
  return std::strtof(value, nullptr);
}

template <>
inline double from_string<double>(const char* value, bool force_hex) {
  if (force_hex || std::strstr(value, "0x") == value ||
      std::strchr(value, 'h') != nullptr) {
    union {
      uint64_t ui;
      double dbl;
    } v;
    v.ui = from_string<uint64_t>(value, force_hex);
    return v.dbl;
  }
  return std::strtod(value, nullptr);
}

template <>
inline vec128_t from_string<vec128_t>(const char* value, bool force_hex) {
  vec128_t v;
  char* p = const_cast<char*>(value);
  bool hex_mode = force_hex;
  if (*p == '[') {
    hex_mode = true;
    ++p;
  } else if (*p == '(') {
    hex_mode = false;
    ++p;
  } else {
    // Assume hex?
    hex_mode = true;
    ++p;
  }
  if (hex_mode) {
    v.i32[0] = std::strtoul(p, &p, 16);
    while (*p == ' ' || *p == ',') ++p;
    v.i32[1] = std::strtoul(p, &p, 16);
    while (*p == ' ' || *p == ',') ++p;
    v.i32[2] = std::strtoul(p, &p, 16);
    while (*p == ' ' || *p == ',') ++p;
    v.i32[3] = std::strtoul(p, &p, 16);
  } else {
    v.f32[0] = std::strtof(p, &p);
    while (*p == ' ' || *p == ',') ++p;
    v.f32[1] = std::strtof(p, &p);
    while (*p == ' ' || *p == ',') ++p;
    v.f32[2] = std::strtof(p, &p);
    while (*p == ' ' || *p == ',') ++p;
    v.f32[3] = std::strtof(p, &p);
  }
  return v;
}

#if XE_ARCH_AMD64

// TODO(DrChat): ?? Why is this here? Force the caller to use vec128.
template <>
inline __m128 from_string<__m128>(const char* value, bool force_hex) {
  __m128 v;
  float f[4];
  uint32_t u;
  char* p = const_cast<char*>(value);
  bool hex_mode = force_hex;
  if (*p == '[') {
    hex_mode = true;
    ++p;
  } else if (*p == '(') {
    hex_mode = false;
    ++p;
  } else {
    // Assume hex?
    hex_mode = true;
    ++p;
  }
  if (hex_mode) {
    u = std::strtoul(p, &p, 16);
    f[0] = *reinterpret_cast<float*>(&u);
    while (*p == ' ' || *p == ',') ++p;
    u = std::strtoul(p, &p, 16);
    f[1] = *reinterpret_cast<float*>(&u);
    while (*p == ' ' || *p == ',') ++p;
    u = std::strtoul(p, &p, 16);
    f[2] = *reinterpret_cast<float*>(&u);
    while (*p == ' ' || *p == ',') ++p;
    u = std::strtoul(p, &p, 16);
    f[3] = *reinterpret_cast<float*>(&u);
  } else {
    f[0] = std::strtof(p, &p);
    while (*p == ' ' || *p == ',') ++p;
    f[1] = std::strtof(p, &p);
    while (*p == ' ' || *p == ',') ++p;
    f[2] = std::strtof(p, &p);
    while (*p == ' ' || *p == ',') ++p;
    f[3] = std::strtof(p, &p);
  }
  v = _mm_loadu_ps(f);
  return v;
}

#endif

template <typename T>
inline T from_string(const std::string& value, bool force_hex = false) {
  return from_string<T>(value.c_str(), force_hex);
}

}  // namespace string_util
}  // namespace xe

#endif  // XENIA_BASE_STRING_UTIL_H_
