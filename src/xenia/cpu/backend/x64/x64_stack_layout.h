/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_X64_X64_STACK_LAYOUT_H_
#define XENIA_CPU_BACKEND_X64_X64_STACK_LAYOUT_H_

#include "xenia/base/vec128.h"
#include "xenia/cpu/backend/x64/x64_backend.h"
#include "xenia/cpu/backend/x64/x64_emitter.h"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

class StackLayout {
 public:
  /**
   * Stack Layout
   * ----------------------------
   * NOTE: stack must always be 16b aligned.
   *
   * Thunk stack:
   *  +------------------+
   *  | arg temp, 3 * 8  | rsp + 0
   *  |                  |
   *  |                  |
   *  +------------------+
   *  | scratch, 16b     | rsp + 24
   *  |                  |
   *  +------------------+
   *  | rbx              | rsp + 40
   *  +------------------+
   *  | rcx / context    | rsp + 48
   *  +------------------+
   *  | rbp              | rsp + 56
   *  +------------------+
   *  | rsi              | rsp + 64
   *  +------------------+
   *  | rdi              | rsp + 72
   *  +------------------+
   *  | r12              | rsp + 80
   *  +------------------+
   *  | r13              | rsp + 88
   *  +------------------+
   *  | r14              | rsp + 96
   *  +------------------+
   *  | r15              | rsp + 104
   *  +------------------+
   *  | xmm6/0           | rsp + 112
   *  |                  |
   *  +------------------+
   *  | xmm7/1           | rsp + 128
   *  |                  |
   *  +------------------+
   *  | xmm8/2           | rsp + 144
   *  |                  |
   *  +------------------+
   *  | xmm9/3           | rsp + 160
   *  |                  |
   *  +------------------+
   *  | xmm10/4          | rsp + 176
   *  |                  |
   *  +------------------+
   *  | xmm11/5          | rsp + 192
   *  |                  |
   *  +------------------+
   *  | xmm12            | rsp + 208
   *  |                  |
   *  +------------------+
   *  | xmm13            | rsp + 224
   *  |                  |
   *  +------------------+
   *  | xmm14            | rsp + 240
   *  |                  |
   *  +------------------+
   *  | xmm15            | rsp + 256
   *  |                  |
   *  +------------------+
   *  | scratch, 8b      | rsp + 272
   *  |                  |
   *  +------------------+
   *  | (return address) | rsp + 280
   *  +------------------+
   *  | (rcx home)       | rsp + 288
   *  +------------------+
   *  | (rdx home)       | rsp + 296
   *  +------------------+
   */
  XEPACKEDSTRUCT(Thunk, {
    uint64_t arg_temp[3];
    uint8_t scratch[16];
    uint64_t r[10];
    vec128_t xmm[10];
    uint64_t dummy;
  });
  static_assert(sizeof(Thunk) % 16 == 0,
                "sizeof(Thunk) must be a multiple of 16!");
  static const size_t THUNK_STACK_SIZE = sizeof(Thunk) + 8;

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
  static const size_t GUEST_STACK_SIZE = 104;
  static const size_t GUEST_CTX_HOME = 80;
  static const size_t GUEST_RET_ADDR = 88;
  static const size_t GUEST_CALL_RET_ADDR = 96;
};

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_X64_X64_STACK_LAYOUT_H_
