/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/frontend/ppc_context.h"

#include <cstdlib>

namespace xe {
namespace cpu {
namespace frontend {

uint64_t ParseInt64(const char* value) {
  return std::strtoull(value, nullptr, 0);
}

double ParseFloat64(const char* value) {
  if (strstr(value, "0x") == value) {
    union {
      uint64_t ui;
      double dbl;
    } v;
    v.ui = ParseInt64(value);
    return v.dbl;
  }
  return std::strtod(value, nullptr);
}

vec128_t ParseVec128(const char* value) {
  vec128_t v;
  char* p = const_cast<char*>(value);
  if (*p == '[') ++p;
  v.i32[0] = std::strtoul(p, &p, 16);
  while (*p == ' ' || *p == ',') ++p;
  v.i32[1] = std::strtoul(p, &p, 16);
  while (*p == ' ' || *p == ',') ++p;
  v.i32[2] = std::strtoul(p, &p, 16);
  while (*p == ' ' || *p == ',') ++p;
  v.i32[3] = std::strtoul(p, &p, 16);
  return v;
}

void PPCContext::SetRegFromString(const char* name, const char* value) {
  int n;
  if (sscanf(name, "r%d", &n) == 1) {
    this->r[n] = ParseInt64(value);
  } else if (sscanf(name, "f%d", &n) == 1) {
    this->f[n] = ParseFloat64(value);
  } else if (sscanf(name, "v%d", &n) == 1) {
    this->v[n] = ParseVec128(value);
  } else {
    printf("Unrecognized register name: %s\n", name);
  }
}

bool PPCContext::CompareRegWithString(const char* name, const char* value,
                                      char* out_value, size_t out_value_size) {
  int n;
  if (sscanf(name, "r%d", &n) == 1) {
    uint64_t expected = ParseInt64(value);
    if (this->r[n] != expected) {
      snprintf(out_value, out_value_size, "%016llX", this->r[n]);
      return false;
    }
    return true;
  } else if (sscanf(name, "f%d", &n) == 1) {
    double expected = ParseFloat64(value);
    // TODO(benvanik): epsilon
    if (this->f[n] != expected) {
      snprintf(out_value, out_value_size, "%f", this->f[n]);
      return false;
    }
    return true;
  } else if (sscanf(name, "v%d", &n) == 1) {
    vec128_t expected = ParseVec128(value);
    if (this->v[n] != expected) {
      snprintf(out_value, out_value_size, "[%.8X, %.8X, %.8X, %.8X]",
               this->v[n].i32[0], this->v[n].i32[1], this->v[n].i32[2],
               this->v[n].i32[3]);
      return false;
    }
    return true;
  } else {
    printf("Unrecognized register name: %s\n", name);
    return false;
  }
}

}  // namespace frontend
}  // namespace cpu
}  // namespace xe
