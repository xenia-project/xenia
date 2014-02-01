/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_X64_X64_THUNK_EMITTER_H_
#define XENIA_CPU_X64_X64_THUNK_EMITTER_H_

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
 *  +------------------+
 *  | scratch, 56b     | rsp + 0
 *  |                  |
 *  |       ....       |
 *  |                  |
 *  |                  |
 *  +------------------+
 *  | rbx              | rsp + 56
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
 */

class StackLayout {
public:
  const static size_t GUEST_STACK_SIZE = 120;

  const static size_t THUNK_STACK_SIZE = 120;

  const static size_t RETURN_ADDRESS = 120;
  const static size_t RCX_HOME = 128;
  const static size_t RDX_HOME = 136;
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


#endif  // XENIA_CPU_X64_X64_THUNK_EMITTER_H_
