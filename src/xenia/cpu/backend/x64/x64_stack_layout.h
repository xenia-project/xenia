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

#include "xenia/cpu/backend/x64/x64_backend.h"
#include "xenia/cpu/backend/x64/x64_emitter.h"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

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

class StackLayout {
 public:
  static const size_t THUNK_STACK_SIZE = 280;

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
