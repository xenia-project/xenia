/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_X64_THUNK_EMITTER_H_
#define ALLOY_BACKEND_X64_X64_THUNK_EMITTER_H_

#include <alloy/core.h>
#include <alloy/backend/x64/x64_backend.h>
#include <alloy/backend/x64/x64_emitter.h>

namespace alloy {
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
 *  | scratch, 32b     | rsp + 32
 *  |                  |
 *  +------------------+
 *  | rcx / context    | rsp + 64
 *  +------------------+
 *  | guest ret addr   | rsp + 72
 *  +------------------+
 *  | call ret addr    | rsp + 80
 *  +------------------+
 *    ... locals ...
 *  +------------------+
 *  | (return address) |
 *  +------------------+
 *
 */

class StackLayout {
 public:
  const static size_t THUNK_STACK_SIZE = 120;

  const static size_t GUEST_STACK_SIZE = 88;
  const static size_t GUEST_RCX_HOME = 64;
  const static size_t GUEST_RET_ADDR = 72;
  const static size_t GUEST_CALL_RET_ADDR = 80;
};

class X64ThunkEmitter : public X64Emitter {
 public:
  X64ThunkEmitter(X64Backend* backend, XbyakAllocator* allocator);
  virtual ~X64ThunkEmitter();

  // Call a generated function, saving all stack parameters.
  HostToGuestThunk EmitHostToGuestThunk();

  // Function that guest code can call to transition into host code.
  GuestToHostThunk EmitGuestToHostThunk();
};

}  // namespace x64
}  // namespace backend
}  // namespace alloy

#endif  // ALLOY_BACKEND_X64_X64_THUNK_EMITTER_H_
