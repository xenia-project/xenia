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
   *  | arg temp, 3 * 8  | arg temp, 3 * 8  | rsp + 0x000
   *  |                  |                  |
   *  |                  |                  |
   *  +------------------+------------------+
   *  | rbx              | (unused)         | rsp + 0x018
   *  +------------------+------------------+
   *  | rbp              | rcx              | rsp + 0x020
   *  +------------------+------------------+
   *  | rcx (Win32)      | rdx              | rsp + 0x028
   *  +------------------+------------------+
   *  | rsi (Win32)      | rsi (Linux)      | rsp + 0x030
   *  +------------------+------------------+
   *  | rdi (Win32)      | rdi (Linux)      | rsp + 0x038
   *  +------------------+------------------+
   *  | r12              | r8               | rsp + 0x040
   *  +------------------+------------------+
   *  | r13              | r9               | rsp + 0x048
   *  +------------------+------------------+
   *  | r14              | r10              | rsp + 0x050
   *  +------------------+------------------+
   *  | r15              | r11              | rsp + 0x058
   *  +------------------+------------------+
   *  | xmm6 (Win32)     | (unused)         | rsp + 0x060
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm7 (Win32)     | xmm1             | rsp + 0x070
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm8 (Win32)     | xmm2             | rsp + 0x080
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm9 (Win32)     | xmm3             | rsp + 0x090
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm10 (Win32)    | xmm4             | rsp + 0x0A0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm11 (Win32)    | xmm5             | rsp + 0x0B0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm12 (Win32)    | (unused)         | rsp + 0x0C0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm13 (Win32)    | (unused)         | rsp + 0x0D0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm14 (Win32)    | (unused)         | rsp + 0x0E0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | xmm15 (Win32)    | (unused)         | rsp + 0x0F0
   *  |                  |                  |
   *  +------------------+------------------+
   *  | (return address) | (return address) | rsp + 0x100
   *  +------------------+------------------+
   *  | (rcx home)       | (rcx home)       | rsp + 0x108
   *  +------------------+------------------+
   *  | (rdx home)       | (rdx home)       | rsp + 0x110
   *  +------------------+------------------+
   */
  XEPACKEDSTRUCT(Thunk, {
    uint64_t arg_temp[3];
    uint64_t r[19];
    vec128_t xmm[31];
  });
  static_assert(sizeof(Thunk) % 16 == 0,
                "sizeof(Thunk) must be a multiple of 16!");
  static const size_t THUNK_STACK_SIZE = sizeof(Thunk);

  /**
   *
   *
   * Guest stack:
   *  +------------------+
   *  | arg temp, 3 * 8  | rsp + 0
   *  |                  |
   *  |                  |
   *  +------------------+
   *  | scratch, 48b     | rsp + 32
   *  |                  |
   *  +------------------+
   *  | rcx / context    | rsp + 80
   *  +------------------+
   *  | guest ret addr   | rsp + 88
   *  +------------------+
   *  | call ret addr    | rsp + 96
   *  +------------------+
   *    ... locals ...
   *  +------------------+
   *  | (return address) |
   *  +------------------+
   *
   */
  static const size_t GUEST_STACK_SIZE = 96;
  static const size_t GUEST_CTX_HOME = 80;
  static const size_t GUEST_RET_ADDR = 88;
  static const size_t GUEST_CALL_RET_ADDR = 96;
};

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_STACK_LAYOUT_H_
