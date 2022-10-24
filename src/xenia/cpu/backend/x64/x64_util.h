/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_X64_X64_UTIL_H_
#define XENIA_CPU_BACKEND_X64_X64_UTIL_H_

#include "xenia/base/vec128.h"
#include "xenia/cpu/backend/x64/x64_backend.h"
#include "xenia/cpu/backend/x64/x64_emitter.h"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

// Used to generate ternary logic truth-tables for vpternlog
// Use these to directly refer to terms and perform binary operations upon them
// and the resulting value will be the ternary lookup table
// ex:
//  (TernaryOperand::a | ~TernaryOperand::b) & TernaryOperand::c
//      = 0b10100010
//      = 0xa2
//  vpternlog a, b, c, 0xa2
//
//  ~(TernaryOperand::a ^ TernaryOperand::b) & TernaryOperand::c
//      = 0b10000010
//      = 0x82
//  vpternlog a, b, c, 0x82
namespace TernaryOperand {
constexpr uint8_t a = 0b11110000;
constexpr uint8_t b = 0b11001100;
constexpr uint8_t c = 0b10101010;
}  // namespace TernaryOperand

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_X64_X64_UTIL_H_
