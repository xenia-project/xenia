/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_STACK_LAYOUT_H_
#define XENIA_CPU_BACKEND_A64_A64_STACK_LAYOUT_H_

#include "xenia/base/vec128.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/backend/a64/a64_emitter.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

class StackLayout {
 public:
  /**
   * Stack Layout
   * ----------------------------
   * NOTE: stack must always be 16b aligned.
   *
   * Thunk stack:
   *      Non-Volatile         Volatile
   *  +------------------+------------------+
   *  | arg temp, 3 * 8  | arg temp, 3 * 8  | xsp + 0x000
   *  |                  |                  |
   *  |                  |                  |
   *  +------------------+------------------+
   *  | rbx              | (unused)         | xsp + 0x018
   *  +------------------+------------------+
   *  | rbp              | X1               | xsp + 0x020
   *  +------------------+------------------+
   *  | rcx (Win32)      | X2               | xsp + 0x028
   *  +------------------+------------------+
   *  | rsi (Win32)      | X3               | xsp + 0x030
   *  +------------------+------------------+
   *  | rdi (Win32)      | X4               | xsp + 0x038
   *  +------------------+------------------+
   *  | r12              | X5               | xsp + 0x040
   *  +------------------+------------------+
   *  | r13              | X6               | xsp + 0x048
   *  +------------------+------------------+
   *  | r14              | X7               | xsp + 0x050
   *  +------------------+------------------+
   *  | r15              | X8               | xsp + 0x058
   *  +------------------+------------------+
   *  | xmm6 (Win32)     | X9               | xsp + 0x060
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm7 (Win32)     | X10              | xsp + 0x070
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm8 (Win32)     | X11              | xsp + 0x080
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm9 (Win32)     | X12              | xsp + 0x090
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm10 (Win32)    | X13              | xsp + 0x0A0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm11 (Win32)    | X14              | xsp + 0x0B0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm12 (Win32)    | X15              | xsp + 0x0C0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm13 (Win32)    | X16              | xsp + 0x0D0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm14 (Win32)    | X17              | xsp + 0x0E0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm15 (Win32)    | X18              | xsp + 0x0F0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | (return address) | (return address) | xsp + 0x100
   *  +------------------+------------------+
   *  | (rcx home)       | (rcx home)       | xsp + 0x108
   *  +------------------+------------------+
   *  | (rdx home)       | (rdx home)       | xsp + 0x110
   *  +------------------+------------------+
   */
  XEPACKEDSTRUCT(Thunk, {
    uint64_t arg_temp[3];
    uint64_t r[17];
    vec128_t xmm[22];
  });
  static_assert(sizeof(Thunk) % 16 == 0,
                "sizeof(Thunk) must be a multiple of 16!");
  static const size_t THUNK_STACK_SIZE = sizeof(Thunk) + 16;

  /**
   *
   *
   * Guest stack:
   *  +------------------+
   *  | arg temp, 3 * 8  | xsp + 0
   *  |                  |
   *  |                  |
   *  +------------------+
   *  | scratch, 48b     | xsp + 32
   *  |                  |
   *  +------------------+
   *  | X0  / context    | xsp + 80
   *  +------------------+
   *  | guest ret addr   | xsp + 88
   *  +------------------+
   *  | call ret addr    | xsp + 96
   *  +------------------+
   *    ... locals ...
   *  +------------------+
   *  | (return address) |
   *  +------------------+
   *
   */
  static const size_t GUEST_STACK_SIZE = 96 + 16;
  static const size_t GUEST_CTX_HOME = 80;
  static const size_t GUEST_RET_ADDR = 88;
  static const size_t GUEST_CALL_RET_ADDR = 96;
};

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_STACK_LAYOUT_H_
