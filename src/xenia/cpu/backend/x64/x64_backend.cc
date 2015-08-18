/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/x64/x64_backend.h"

#include "xenia/cpu/backend/x64/x64_assembler.h"
#include "xenia/cpu/backend/x64/x64_code_cache.h"
#include "xenia/cpu/backend/x64/x64_emitter.h"
#include "xenia/cpu/backend/x64/x64_function.h"
#include "xenia/cpu/backend/x64/x64_sequences.h"
#include "xenia/cpu/backend/x64/x64_stack_layout.h"
#include "xenia/cpu/processor.h"

DEFINE_bool(
    enable_haswell_instructions, true,
    "Uses the AVX2/FMA/etc instructions on Haswell processors, if available.");

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

class X64ThunkEmitter : public X64Emitter {
 public:
  X64ThunkEmitter(X64Backend* backend, XbyakAllocator* allocator);
  ~X64ThunkEmitter() override;
  HostToGuestThunk EmitHostToGuestThunk();
  GuestToHostThunk EmitGuestToHostThunk();
  ResolveFunctionThunk EmitResolveFunctionThunk();
};

X64Backend::X64Backend(Processor* processor)
    : Backend(processor), code_cache_(nullptr), emitter_data_(0) {}

X64Backend::~X64Backend() {
  if (emitter_data_) {
    processor()->memory()->SystemHeapFree(emitter_data_);
    emitter_data_ = 0;
  }
}

bool X64Backend::Initialize() {
  if (!Backend::Initialize()) {
    return false;
  }

  RegisterSequences();

  // Need movbe to do advanced LOAD/STORE tricks.
  if (FLAGS_enable_haswell_instructions) {
    Xbyak::util::Cpu cpu;
    machine_info_.supports_extended_load_store =
        cpu.has(Xbyak::util::Cpu::tMOVBE);
  } else {
    machine_info_.supports_extended_load_store = false;
  }

  auto& gprs = machine_info_.register_sets[0];
  gprs.id = 0;
  std::strcpy(gprs.name, "gpr");
  gprs.types = MachineInfo::RegisterSet::INT_TYPES;
  gprs.count = X64Emitter::GPR_COUNT;

  auto& xmms = machine_info_.register_sets[1];
  xmms.id = 1;
  std::strcpy(xmms.name, "xmm");
  xmms.types = MachineInfo::RegisterSet::FLOAT_TYPES |
               MachineInfo::RegisterSet::VEC_TYPES;
  xmms.count = X64Emitter::XMM_COUNT;

  code_cache_ = X64CodeCache::Create();
  Backend::code_cache_ = code_cache_.get();
  if (!code_cache_->Initialize()) {
    return false;
  }

  // Generate thunks used to transition between jitted code and host code.
  XbyakAllocator allocator;
  X64ThunkEmitter thunk_emitter(this, &allocator);
  host_to_guest_thunk_ = thunk_emitter.EmitHostToGuestThunk();
  guest_to_host_thunk_ = thunk_emitter.EmitGuestToHostThunk();
  resolve_function_thunk_ = thunk_emitter.EmitResolveFunctionThunk();

  // Set the code cache to use the ResolveFunction thunk for default
  // indirections.
  assert_zero(uint64_t(resolve_function_thunk_) & 0xFFFFFFFF00000000ull);
  code_cache_->set_indirection_default(
      uint32_t(uint64_t(resolve_function_thunk_)));

  // Allocate some special indirections.
  code_cache_->CommitExecutableRange(0x9FFF0000, 0x9FFFFFFF);

  // Allocate emitter constant data.
  emitter_data_ = X64Emitter::PlaceData(processor()->memory());

  return true;
}

void X64Backend::CommitExecutableRange(uint32_t guest_low,
                                       uint32_t guest_high) {
  code_cache_->CommitExecutableRange(guest_low, guest_high);
}

std::unique_ptr<Assembler> X64Backend::CreateAssembler() {
  return std::make_unique<X64Assembler>(this);
}

std::unique_ptr<GuestFunction> X64Backend::CreateGuestFunction(
    Module* module, uint32_t address) {
  return std::make_unique<X64Function>(module, address);
}

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
  // r10 = arg2

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
  mov(r9, r10);
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

// X64Emitter handles actually resolving functions.
extern "C" uint64_t ResolveFunction(void* raw_context, uint32_t target_address);

ResolveFunctionThunk X64ThunkEmitter::EmitResolveFunctionThunk() {
  // ebx = target PPC address
  // rcx = context

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

  mov(rdx, rbx);
  mov(rax, uint64_t(&ResolveFunction));
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
  jmp(rax);

  void* fn = Emplace(stack_size);
  return (ResolveFunctionThunk)fn;
}

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
