/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_UTIL_H_
#define XENIA_CPU_BACKEND_A64_A64_UTIL_H_

#include "xenia/base/vec128.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/backend/a64/a64_emitter.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

// Attempts to convert an fp32 bit-value into an fp8-immediate value for FMOV
// returns false if the value cannot be represented
// C2.2.3 Modified immediate constants in A64  ing-point instructions
// abcdefgh
//    V
// aBbbbbbc defgh000 00000000 00000000
// B = NOT(b)
constexpr bool f32_to_fimm8(uint32_t u32, oaknut::FImm8& fp8) {
  const uint32_t sign = (u32 >> 31) & 1;
  int32_t exp = ((u32 >> 23) & 0xff) - 127;
  int64_t mantissa = u32 & 0x7fffff;

  // Too many mantissa bits
  if (mantissa & 0x7ffff) {
    return false;
  }
  // Too many exp bits
  if (exp < -3 || exp > 4) {
    return false;
  }

  // mantissa = (16 + e:f:g:h) / 16.
  mantissa >>= 19;
  if ((mantissa & 0b1111) != mantissa) {
    return false;
  }

  // exp = (NOT(b):c:d) - 3
  exp = ((exp + 3) & 0b111) ^ 0b100;

  fp8 = oaknut::FImm8(sign, exp, uint8_t(mantissa));
  return true;
}

// Attempts to convert an fp64 bit-value into an fp8-immediate value for FMOV
// returns false if the value cannot be represented
// C2.2.3 Modified immediate constants in A64 floating-point instructions
// abcdefgh
//    V
// aBbbbbbb bbcdefgh 00000000 00000000 00000000 00000000 00000000 00000000
// B = NOT(b)
constexpr bool f64_to_fimm8(uint64_t u64, oaknut::FImm8& fp8) {
  const uint32_t sign = (u64 >> 63) & 1;
  int32_t exp = ((u64 >> 52) & 0x7ff) - 1023;
  int64_t mantissa = u64 & 0xfffffffffffffULL;

  // Too many mantissa bits
  if (mantissa & 0xffffffffffffULL) {
    return false;
  }
  // Too many exp bits
  if (exp < -3 || exp > 4) {
    return false;
  }

  // mantissa = (16 + e:f:g:h) / 16.
  mantissa >>= 48;
  if ((mantissa & 0b1111) != mantissa) {
    return false;
  }

  // exp = (NOT(b):c:d) - 3
  exp = ((exp + 3) & 0b111) ^ 0b100;

  fp8 = oaknut::FImm8(sign, exp, uint8_t(mantissa));
  return true;
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_UTIL_H_
