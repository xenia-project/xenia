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
 *  | scratch, 16b     | rsp + 32
 *  |                  |
 *  +------------------+
 *  | rbx              | rsp + 48
 *  +------------------+
 *  | rcx / context    | rsp + 56
 *  +------------------+
 *  | rbp              | rsp + 64
 *  +------------------+
 *  | rsi              | rsp + 72
 *  +------------------+
 *  | rdi              | rsp + 80
 *  +------------------+
 *  | r12              | rsp + 88
 *  +------------------+
 *  | r13              | rsp + 96
 *  +------------------+
 *  | r14              | rsp + 104
 *  +------------------+
 *  | r15              | rsp + 112
 *  +------------------+
 *  | (return address) | rsp + 120
 *  +------------------+
 *  | (rcx home)       | rsp + 128
 *  +------------------+
 *  | (rdx home)       | rsp + 136
 *  +------------------+
 *
 *
 * TODO:
 *  +------------------+
 *  | xmm6             | rsp + 128
 *  |                  |
 *  +------------------+
 *  | xmm7             | rsp + 144
 *  |                  |
 *  +------------------+
 *  | xmm8             | rsp + 160
 *  |                  |
 *  +------------------+
 *  | xmm9             | rsp + 176
 *  |                  |
 *  +------------------+
 *  | xmm10            | rsp + 192
 *  |                  |
 *  +------------------+
 *  | xmm11            | rsp + 208
 *  |                  |
 *  +------------------+
 *  | xmm12            | rsp + 224
 *  |                  |
 *  +------------------+
 *  | xmm13            | rsp + 240
 *  |                  |
 *  +------------------+
 *  | xmm14            | rsp + 256
 *  |                  |
 *  +------------------+
 *  | xmm15            | rsp + 272
 *  |                  |
 *  +------------------+
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
  static const size_t THUNK_STACK_SIZE = 120;

  static const size_t GUEST_STACK_SIZE = 104;
  static const size_t GUEST_RCX_HOME = 80;
  static const size_t GUEST_RET_ADDR = 88;
  static const size_t GUEST_CALL_RET_ADDR = 96;
};

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_X64_X64_STACK_LAYOUT_H_
