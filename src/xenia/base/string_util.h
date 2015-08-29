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
#include <cstring>
#include <string>

#include "xenia/base/platform.h"
#include "xenia/base/vec128.h"

namespace xe {
namespace string_util {

inline std::string to_hex_string(uint32_t value) {
  char buffer[21];
  std::snprintf(buffer, sizeof(buffer), "%08" PRIX32, value);
  return std::string(buffer);
}

inline std::string to_hex_string(uint64_t value) {
  char buffer[21];
  std::snprintf(buffer, sizeof(buffer), "%016" PRIX64, value);
  return std::string(buffer);
}

inline std::string to_hex_string(double value) {
  union {
    uint64_t ui;
    double dbl;
  } v;
  v.dbl = value;
  return to_hex_string(v.ui);
}

inline std::string to_hex_string(const vec128_t& value) {
  char buffer[128];
  std::snprintf(buffer, sizeof(buffer), "[%.8X, %.8X, %.8X, %.8X]",
                value.u32[0], value.u32[1], value.u32[2], value.u32[3]);
  return std::string(buffer);
}

inline std::string to_hex_string(const __m128& value) {
  char buffer[128];
  std::snprintf(buffer, sizeof(buffer), "[%.8X, %.8X, %.8X, %.8X]",
                value.m128_u32[0], value.m128_u32[1], value.m128_u32[2],
                value.m128_u32[3]);
  return std::string(buffer);
}

template <typename T>
inline T from_string(const char* value);

template <>
inline uint64_t from_string<uint64_t>(const char* value) {
  if (std::strchr(value, 'h') != nullptr) {
    return std::strtoull(value, nullptr, 16);
  } else {
    return std::strtoull(value, nullptr, 0);
  }
}

template <>
inline double from_string<double>(const char* value) {
  if (std::strstr(value, "0x") == value || std::strchr(value, 'h') != nullptr) {
    union {
      uint64_t ui;
      double dbl;
    } v;
    v.ui = from_string<uint64_t>(value);
    return v.dbl;
  }
  return std::strtod(value, nullptr);
}

template <>
inline vec128_t from_string<vec128_t>(const char* value) {
  vec128_t v;
  char* p = const_cast<char*>(value);
  bool hex_mode = false;
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

template <>
inline __m128 from_string<__m128>(const char* value) {
  __m128 v;
  char* p = const_cast<char*>(value);
  bool hex_mode = false;
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
    v.m128_u32[0] = std::strtoul(p, &p, 16);
    while (*p == ' ' || *p == ',') ++p;
    v.m128_u32[1] = std::strtoul(p, &p, 16);
    while (*p == ' ' || *p == ',') ++p;
    v.m128_u32[2] = std::strtoul(p, &p, 16);
    while (*p == ' ' || *p == ',') ++p;
    v.m128_u32[3] = std::strtoul(p, &p, 16);
  } else {
    v.m128_f32[0] = std::strtof(p, &p);
    while (*p == ' ' || *p == ',') ++p;
    v.m128_f32[1] = std::strtof(p, &p);
    while (*p == ' ' || *p == ',') ++p;
    v.m128_f32[2] = std::strtof(p, &p);
    while (*p == ' ' || *p == ',') ++p;
    v.m128_f32[3] = std::strtof(p, &p);
  }
  return v;
}

template <typename T>
inline T from_string(const std::string& value) {
  return from_string<T>(value.c_str());
}

}  // namespace string_util
}  // namespace xe

#endif  // XENIA_BASE_STRING_UTIL_H_
