/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/x64_thunk_emitter.h>

#include <third_party/xbyak/xbyak/xbyak.h>

namespace alloy {
namespace backend {
namespace x64 {

using namespace Xbyak;

X64ThunkEmitter::X64ThunkEmitter(X64Backend* backend, XbyakAllocator* allocator)
    : X64Emitter(backend, allocator) {}

X64ThunkEmitter::~X64ThunkEmitter() {}

HostToGuestThunk X64ThunkEmitter::EmitHostToGuestThunk() {
  // rcx = target
  // rdx = arg0
  // r8 = arg1

  const size_t stack_size = StackLayout::THUNK_STACK_SIZE;
  // rsp + 0 = return address
  mov(qword[rsp + 8 * 3], r8);
  mov(qword[rsp + 8 * 2], rdx);
  mov(qword[rsp + 8 * 1], rcx);
  sub(rsp, stack_size);

  mov(qword[rsp + 48], rbx);
  mov(qword[rsp + 56], rcx);
  mov(qword[rsp + 64], rbp);
  mov(qword[rsp + 72], rsi);
  mov(qword[rsp + 80], rdi);
  mov(qword[rsp + 88], r12);
  mov(qword[rsp + 96], r13);
  mov(qword[rsp + 104], r14);
  mov(qword[rsp + 112], r15);

  /*movaps(ptr[rsp + 128], xmm6);
  movaps(ptr[rsp + 144], xmm7);
  movaps(ptr[rsp + 160], xmm8);
  movaps(ptr[rsp + 176], xmm9);
  movaps(ptr[rsp + 192], xmm10);
  movaps(ptr[rsp + 208], xmm11);
  movaps(ptr[rsp + 224], xmm12);
  movaps(ptr[rsp + 240], xmm13);
  movaps(ptr[rsp + 256], xmm14);
  movaps(ptr[rsp + 272], xmm15);*/

  mov(rax, rcx);
  mov(rcx, rdx);
  mov(rdx, r8);
  call(rax);

  /*movaps(xmm6, ptr[rsp + 128]);
  movaps(xmm7, ptr[rsp + 144]);
  movaps(xmm8, ptr[rsp + 160]);
  movaps(xmm9, ptr[rsp + 176]);
  movaps(xmm10, ptr[rsp + 192]);
  movaps(xmm11, ptr[rsp + 208]);
  movaps(xmm12, ptr[rsp + 224]);
  movaps(xmm13, ptr[rsp + 240]);
  movaps(xmm14, ptr[rsp + 256]);
  movaps(xmm15, ptr[rsp + 272]);*/

  mov(rbx, qword[rsp + 48]);
  mov(rcx, qword[rsp + 56]);
  mov(rbp, qword[rsp + 64]);
  mov(rsi, qword[rsp + 72]);
  mov(rdi, qword[rsp + 80]);
  mov(r12, qword[rsp + 88]);
  mov(r13, qword[rsp + 96]);
  mov(r14, qword[rsp + 104]);
  mov(r15, qword[rsp + 112]);

  add(rsp, stack_size);
  mov(rcx, qword[rsp + 8 * 1]);
  mov(rdx, qword[rsp + 8 * 2]);
  mov(r8, qword[rsp + 8 * 3]);
  ret();

  void* fn = Emplace(stack_size);
  return (HostToGuestThunk)fn;
}

GuestToHostThunk X64ThunkEmitter::EmitGuestToHostThunk() {
  // rcx = context
  // rdx = target function
  // r8  = arg0
  // r9  = arg1

  const size_t stack_size = StackLayout::THUNK_STACK_SIZE;
  // rsp + 0 = return address
  mov(qword[rsp + 8 * 2], rdx);
  mov(qword[rsp + 8 * 1], rcx);
  sub(rsp, stack_size);

  mov(qword[rsp + 48], rbx);
  mov(qword[rsp + 56], rcx);
  mov(qword[rsp + 64], rbp);
  mov(qword[rsp + 72], rsi);
  mov(qword[rsp + 80], rdi);
  mov(qword[rsp + 88], r12);
  mov(qword[rsp + 96], r13);
  mov(qword[rsp + 104], r14);
  mov(qword[rsp + 112], r15);

  // TODO(benvanik): save things? XMM0-5?

  mov(rax, rdx);
  mov(rdx, r8);
  mov(r8, r9);
  call(rax);

  mov(rbx, qword[rsp + 48]);
  mov(rcx, qword[rsp + 56]);
  mov(rbp, qword[rsp + 64]);
  mov(rsi, qword[rsp + 72]);
  mov(rdi, qword[rsp + 80]);
  mov(r12, qword[rsp + 88]);
  mov(r13, qword[rsp + 96]);
  mov(r14, qword[rsp + 104]);
  mov(r15, qword[rsp + 112]);

  add(rsp, stack_size);
  mov(rcx, qword[rsp + 8 * 1]);
  mov(rdx, qword[rsp + 8 * 2]);
  ret();

  void* fn = Emplace(stack_size);
  return (HostToGuestThunk)fn;
}

}  // namespace x64
}  // namespace backend
}  // namespace alloy
