/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_backend.h"

#include <cstddef>

#include "xenia/base/exception_handler.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/backend/a64/a64_assembler.h"
#include "xenia/cpu/backend/a64/a64_code_cache.h"
#include "xenia/cpu/backend/a64/a64_emitter.h"
#include "xenia/cpu/backend/a64/a64_function.h"
#include "xenia/cpu/backend/a64/a64_sequences.h"
#include "xenia/cpu/backend/a64/a64_stack_layout.h"
#include "xenia/cpu/breakpoint.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/stack_walker.h"

DEFINE_int32(a64_extension_mask, -1,
             "Allow the detection and utilization of specific instruction set "
             "features.\n"
             "    0 = arm64v8\n"
             "   -1 = Detect and utilize all possible processor features\n",
             "a64");

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

using namespace oaknut::util;

class A64ThunkEmitter : public A64Emitter {
 public:
  A64ThunkEmitter(A64Backend* backend);
  ~A64ThunkEmitter() override;
  HostToGuestThunk EmitHostToGuestThunk();
  GuestToHostThunk EmitGuestToHostThunk();
  ResolveFunctionThunk EmitResolveFunctionThunk();

 private:
  // The following four functions provide save/load functionality for registers.
  // They assume at least StackLayout::THUNK_STACK_SIZE bytes have been
  // allocated on the stack.

  // Caller saved:
  // Dont assume these registers will survive a subroutine call
  // x0, v0 is not saved/preserved since this is used to return values from
  // subroutines x1-x15, x30 | d0-d7 and d16-v31
  void EmitSaveVolatileRegs();
  void EmitLoadVolatileRegs();

  // Callee saved:
  // Subroutines must preserve these registers if they intend to use them
  // x19-x30 | d8-d15
  void EmitSaveNonvolatileRegs();
  void EmitLoadNonvolatileRegs();
};

A64Backend::A64Backend() : Backend() {}

A64Backend::~A64Backend() {
  A64Emitter::FreeConstData(emitter_data_);
  ExceptionHandler::Uninstall(&ExceptionCallbackThunk, this);
}

bool A64Backend::Initialize(Processor* processor) {
  if (!Backend::Initialize(processor)) {
    return false;
  }

  auto& gprs = machine_info_.register_sets[0];
  gprs.id = 0;
  std::strcpy(gprs.name, "x");
  gprs.types = MachineInfo::RegisterSet::INT_TYPES;
  gprs.count = A64Emitter::GPR_COUNT;

  auto& fprs = machine_info_.register_sets[1];
  fprs.id = 1;
  std::strcpy(fprs.name, "v");
  fprs.types = MachineInfo::RegisterSet::FLOAT_TYPES |
               MachineInfo::RegisterSet::VEC_TYPES;
  fprs.count = A64Emitter::FPR_COUNT;

  code_cache_ = A64CodeCache::Create();
  Backend::code_cache_ = code_cache_.get();
  if (!code_cache_->Initialize()) {
    return false;
  }

  // Generate thunks used to transition between jitted code and host code.
  A64ThunkEmitter thunk_emitter(this);
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
  emitter_data_ = A64Emitter::PlaceConstData();

  // Setup exception callback
  ExceptionHandler::Install(&ExceptionCallbackThunk, this);

  return true;
}

void A64Backend::CommitExecutableRange(uint32_t guest_low,
                                       uint32_t guest_high) {
  code_cache_->CommitExecutableRange(guest_low, guest_high);
}

std::unique_ptr<Assembler> A64Backend::CreateAssembler() {
  return std::make_unique<A64Assembler>(this);
}

std::unique_ptr<GuestFunction> A64Backend::CreateGuestFunction(
    Module* module, uint32_t address) {
  return std::make_unique<A64Function>(module, address);
}
uint64_t A64Backend::CalculateNextHostInstruction(ThreadDebugInfo* thread_info,
                                                  uint64_t current_pc) {
  // TODO(wunkolo): Capstone hookup
  return current_pc += 4;
}

void A64Backend::InstallBreakpoint(Breakpoint* breakpoint) {
  breakpoint->ForEachHostAddress([breakpoint](uint64_t host_address) {
    auto ptr = reinterpret_cast<void*>(host_address);
    auto original_bytes = xe::load_and_swap<uint16_t>(ptr);
    assert_true(original_bytes != 0x0F0B);
    xe::store_and_swap<uint16_t>(ptr, 0x0F0B);
    breakpoint->backend_data().emplace_back(host_address, original_bytes);
  });
}

void A64Backend::InstallBreakpoint(Breakpoint* breakpoint, Function* fn) {
  assert_true(breakpoint->address_type() == Breakpoint::AddressType::kGuest);
  assert_true(fn->is_guest());
  auto guest_function = reinterpret_cast<cpu::GuestFunction*>(fn);
  auto host_address =
      guest_function->MapGuestAddressToMachineCode(breakpoint->guest_address());
  if (!host_address) {
    assert_always();
    return;
  }

  // Assume we haven't already installed a breakpoint in this spot.
  auto ptr = reinterpret_cast<void*>(host_address);
  auto original_bytes = xe::load_and_swap<uint16_t>(ptr);
  assert_true(original_bytes != 0x0F0B);
  xe::store_and_swap<uint16_t>(ptr, 0x0F0B);
  breakpoint->backend_data().emplace_back(host_address, original_bytes);
}

void A64Backend::UninstallBreakpoint(Breakpoint* breakpoint) {
  for (auto& pair : breakpoint->backend_data()) {
    auto ptr = reinterpret_cast<uint8_t*>(pair.first);
    auto instruction_bytes = xe::load_and_swap<uint16_t>(ptr);
    assert_true(instruction_bytes == 0x0F0B);
    xe::store_and_swap<uint16_t>(ptr, static_cast<uint16_t>(pair.second));
  }
  breakpoint->backend_data().clear();
}

bool A64Backend::ExceptionCallbackThunk(Exception* ex, void* data) {
  auto backend = reinterpret_cast<A64Backend*>(data);
  return backend->ExceptionCallback(ex);
}

bool A64Backend::ExceptionCallback(Exception* ex) {
  if (ex->code() != Exception::Code::kIllegalInstruction) {
    // We only care about illegal instructions. Other things will be handled by
    // other handlers (probably). If nothing else picks it up we'll be called
    // with OnUnhandledException to do real crash handling.
    return false;
  }

  // Verify an expected illegal instruction.
  auto instruction_bytes =
      xe::load_and_swap<uint16_t>(reinterpret_cast<void*>(ex->pc()));
  if (instruction_bytes != 0x0F0B) {
    // Not our ud2 - not us.
    return false;
  }

  // Let the processor handle things.
  return processor()->OnThreadBreakpointHit(ex);
}

A64ThunkEmitter::A64ThunkEmitter(A64Backend* backend) : A64Emitter(backend) {}

A64ThunkEmitter::~A64ThunkEmitter() {}

HostToGuestThunk A64ThunkEmitter::EmitHostToGuestThunk() {
  // X0 = target
  // X1 = arg0 (context)
  // X2 = arg1 (guest return address)

  struct _code_offsets {
    size_t prolog;
    size_t prolog_stack_alloc;
    size_t body;
    size_t epilog;
    size_t tail;
  } code_offsets = {};

  const size_t stack_size = StackLayout::THUNK_STACK_SIZE;

  code_offsets.prolog = offset();

  //  mov(qword[rsp + 8 * 3], r8);
  //  mov(qword[rsp + 8 * 2], rdx);
  //  mov(qword[rsp + 8 * 1], rcx);
  //  sub(rsp, stack_size);


  STR(X2, SP, 8 * 3);
  STR(X1, SP, 8 * 2);
  STR(X0, SP, 8 * 1);
  SUB(SP, SP, stack_size);

  code_offsets.prolog_stack_alloc = offset();
  code_offsets.body = offset();

  // Save nonvolatile registers.
  EmitSaveNonvolatileRegs();

  // mov(rax, rcx);
  // mov(rsi, rdx);  // context
  // mov(rcx, r8);   // return address
  // call(rax);
  MOV(X16, X0);
  MOV(A64Emitter::GetContextReg(), X1);  // context
  MOV(X0, X2);                           // return address

  BLR(X16);

  EmitLoadNonvolatileRegs();

  code_offsets.epilog = offset();

  // add(rsp, stack_size);
  // mov(rcx, qword[rsp + 8 * 1]);
  // mov(rdx, qword[rsp + 8 * 2]);
  // mov(r8, qword[rsp + 8 * 3]);
  // ret();

  ADD(SP, SP, stack_size);
  LDR(X0, SP, 8 * 1);
  LDR(X1, SP, 8 * 2);
  LDR(X2, SP, 8 * 3);

  RET();

  code_offsets.tail = offset();

  assert_zero(code_offsets.prolog);
  EmitFunctionInfo func_info = {};
  func_info.code_size.total = offset();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = offset() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;
  func_info.stack_size = stack_size;

  void* fn = Emplace(func_info);
  return (HostToGuestThunk)fn;
}

GuestToHostThunk A64ThunkEmitter::EmitGuestToHostThunk() {
  // X0 = target function
  // X1 = arg0
  // X2 = arg1
  // X3 = arg2

  struct _code_offsets {
    size_t prolog;
    size_t prolog_stack_alloc;
    size_t body;
    size_t epilog;
    size_t tail;
  } code_offsets = {};

  const size_t stack_size = StackLayout::THUNK_STACK_SIZE;

  code_offsets.prolog = offset();

  // rsp + 0 = return address
  // sub(rsp, stack_size);
  SUB(SP, SP, stack_size);

  code_offsets.prolog_stack_alloc = offset();
  code_offsets.body = offset();

  // Save off volatile registers.
  EmitSaveVolatileRegs();

  // mov(rax, rcx);              // function
  // mov(rcx, GetContextReg());  // context
  // call(rax);
  MOV(X16, X0);              // function
  MOV(X0, GetContextReg());  // context
  BLR(X16);

  EmitLoadVolatileRegs();

  code_offsets.epilog = offset();

  // add(rsp, stack_size);
  // ret();
  ADD(SP, SP, stack_size);
  RET();

  code_offsets.tail = offset();

  assert_zero(code_offsets.prolog);
  EmitFunctionInfo func_info = {};
  func_info.code_size.total = offset();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = offset() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;
  func_info.stack_size = stack_size;

  void* fn = Emplace(func_info);
  return (GuestToHostThunk)fn;
}

// A64Emitter handles actually resolving functions.
uint64_t ResolveFunction(void* raw_context, uint64_t target_address);

ResolveFunctionThunk A64ThunkEmitter::EmitResolveFunctionThunk() {
  // Entry:
  // X0 = target PPC address

  // Resolve Function:
  // X0 = context
  // X1 = target PPC address

  struct _code_offsets {
    size_t prolog;
    size_t prolog_stack_alloc;
    size_t body;
    size_t epilog;
    size_t tail;
  } code_offsets = {};

  const size_t stack_size = StackLayout::THUNK_STACK_SIZE;

  code_offsets.prolog = offset();

  // rsp + 0 = return address
  // sub(rsp, stack_size);
  SUB(SP, SP, stack_size);

  code_offsets.prolog_stack_alloc = offset();
  code_offsets.body = offset();

  // Save volatile registers
  EmitSaveVolatileRegs();

  // mov(rcx, rsi);  // context
  // mov(rdx, rbx);
  // mov(rax, reinterpret_cast<uint64_t>(&ResolveFunction));
  // call(rax)
  MOV(X1, X0);
  MOV(X0, GetContextReg());  // context
  MOVP2R(X16, &ResolveFunction);
  BLR(X16);

  EmitLoadVolatileRegs();

  code_offsets.epilog = offset();

  // add(rsp, stack_size);
  // jmp(rax);
  ADD(SP, SP, stack_size);
  BR(X0);

  code_offsets.tail = offset();

  assert_zero(code_offsets.prolog);
  EmitFunctionInfo func_info = {};
  func_info.code_size.total = offset();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = offset() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;
  func_info.stack_size = stack_size;

  void* fn = Emplace(func_info);
  return (ResolveFunctionThunk)fn;
}

void A64ThunkEmitter::EmitSaveVolatileRegs() {
  // Save off volatile registers.
  // Preserve arguments passed to and returned from a subroutine
  // STR(X0, SP, offsetof(StackLayout::Thunk, r[0]));
  STP(X1, X2, SP, offsetof(StackLayout::Thunk, r[0]));
  STP(X3, X4, SP, offsetof(StackLayout::Thunk, r[2]));
  STP(X5, X6, SP, offsetof(StackLayout::Thunk, r[4]));
  STP(X7, X8, SP, offsetof(StackLayout::Thunk, r[6]));
  STP(X9, X10, SP, offsetof(StackLayout::Thunk, r[8]));
  STP(X11, X12, SP, offsetof(StackLayout::Thunk, r[10]));
  STP(X13, X14, SP, offsetof(StackLayout::Thunk, r[12]));
  STP(X15, X30, SP, offsetof(StackLayout::Thunk, r[14]));

  // Preserve arguments passed to and returned from a subroutine
  // STR(Q0, SP, offsetof(StackLayout::Thunk, xmm[0]));
  STP(Q1, Q2, SP, offsetof(StackLayout::Thunk, xmm[0]));
  STP(Q3, Q4, SP, offsetof(StackLayout::Thunk, xmm[2]));
  STP(Q5, Q6, SP, offsetof(StackLayout::Thunk, xmm[4]));
  STP(Q7, Q16, SP, offsetof(StackLayout::Thunk, xmm[6]));
  STP(Q7, Q16, SP, offsetof(StackLayout::Thunk, xmm[6]));
  STP(Q17, Q18, SP, offsetof(StackLayout::Thunk, xmm[8]));
  STP(Q19, Q20, SP, offsetof(StackLayout::Thunk, xmm[10]));
  STP(Q21, Q22, SP, offsetof(StackLayout::Thunk, xmm[12]));
  STP(Q23, Q24, SP, offsetof(StackLayout::Thunk, xmm[14]));
  STP(Q25, Q26, SP, offsetof(StackLayout::Thunk, xmm[16]));
  STP(Q27, Q28, SP, offsetof(StackLayout::Thunk, xmm[18]));
  STP(Q29, Q30, SP, offsetof(StackLayout::Thunk, xmm[20]));
  STR(Q31, SP, offsetof(StackLayout::Thunk, xmm[21]));
}

void A64ThunkEmitter::EmitLoadVolatileRegs() {
  // Preserve arguments passed to and returned from a subroutine
  // LDR(X0, SP, offsetof(StackLayout::Thunk, r[0]));
  LDP(X1, X2, SP, offsetof(StackLayout::Thunk, r[0]));
  LDP(X3, X4, SP, offsetof(StackLayout::Thunk, r[2]));
  LDP(X5, X6, SP, offsetof(StackLayout::Thunk, r[4]));
  LDP(X7, X8, SP, offsetof(StackLayout::Thunk, r[6]));
  LDP(X9, X10, SP, offsetof(StackLayout::Thunk, r[8]));
  LDP(X11, X12, SP, offsetof(StackLayout::Thunk, r[10]));
  LDP(X13, X14, SP, offsetof(StackLayout::Thunk, r[12]));
  LDP(X15, X30, SP, offsetof(StackLayout::Thunk, r[14]));

  // Preserve arguments passed to and returned from a subroutine
  // LDR(Q0, SP, offsetof(StackLayout::Thunk, xmm[0]));
  LDP(Q1, Q2, SP, offsetof(StackLayout::Thunk, xmm[0]));
  LDP(Q3, Q4, SP, offsetof(StackLayout::Thunk, xmm[2]));
  LDP(Q5, Q6, SP, offsetof(StackLayout::Thunk, xmm[4]));
  LDP(Q7, Q16, SP, offsetof(StackLayout::Thunk, xmm[6]));
  LDP(Q7, Q16, SP, offsetof(StackLayout::Thunk, xmm[6]));
  LDP(Q17, Q18, SP, offsetof(StackLayout::Thunk, xmm[8]));
  LDP(Q19, Q20, SP, offsetof(StackLayout::Thunk, xmm[10]));
  LDP(Q21, Q22, SP, offsetof(StackLayout::Thunk, xmm[12]));
  LDP(Q23, Q24, SP, offsetof(StackLayout::Thunk, xmm[14]));
  LDP(Q25, Q26, SP, offsetof(StackLayout::Thunk, xmm[16]));
  LDP(Q27, Q28, SP, offsetof(StackLayout::Thunk, xmm[18]));
  LDP(Q29, Q30, SP, offsetof(StackLayout::Thunk, xmm[20]));
  LDR(Q31, SP, offsetof(StackLayout::Thunk, xmm[21]));
}

void A64ThunkEmitter::EmitSaveNonvolatileRegs() {
  STP(X19, X20, SP, offsetof(StackLayout::Thunk, r[0]));
  STP(X21, X22, SP, offsetof(StackLayout::Thunk, r[2]));
  STP(X23, X24, SP, offsetof(StackLayout::Thunk, r[4]));
  STP(X25, X26, SP, offsetof(StackLayout::Thunk, r[6]));
  STP(X27, X28, SP, offsetof(StackLayout::Thunk, r[8]));
  STP(X29, X30, SP, offsetof(StackLayout::Thunk, r[10]));

  STP(Q8, Q9, SP, offsetof(StackLayout::Thunk, xmm[0]));
  STP(Q10, Q11, SP, offsetof(StackLayout::Thunk, xmm[2]));
  STP(Q12, Q13, SP, offsetof(StackLayout::Thunk, xmm[4]));
  STP(Q14, Q15, SP, offsetof(StackLayout::Thunk, xmm[6]));
}

void A64ThunkEmitter::EmitLoadNonvolatileRegs() {
  LDP(X19, X20, SP, offsetof(StackLayout::Thunk, r[0]));
  LDP(X21, X22, SP, offsetof(StackLayout::Thunk, r[2]));
  LDP(X23, X24, SP, offsetof(StackLayout::Thunk, r[4]));
  LDP(X25, X26, SP, offsetof(StackLayout::Thunk, r[6]));
  LDP(X27, X28, SP, offsetof(StackLayout::Thunk, r[8]));
  LDP(X29, X30, SP, offsetof(StackLayout::Thunk, r[10]));

  LDP(Q8, Q9, SP, offsetof(StackLayout::Thunk, xmm[0]));
  LDP(Q10, Q11, SP, offsetof(StackLayout::Thunk, xmm[2]));
  LDP(Q12, Q13, SP, offsetof(StackLayout::Thunk, xmm[4]));
  LDP(Q14, Q15, SP, offsetof(StackLayout::Thunk, xmm[6]));
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
