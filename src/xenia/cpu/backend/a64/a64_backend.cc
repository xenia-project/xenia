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
  void EmitSaveVolatileRegs();
  void EmitLoadVolatileRegs();
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
  fprs.count = A64Emitter::XMM_COUNT;

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

  // mov(qword[rsp + 8 * 3], r8);
  // mov(qword[rsp + 8 * 2], rdx);
  // mov(qword[rsp + 8 * 1], rcx);
  // sub(rsp, stack_size);
  STR(X2, XSP, 8 * 3);
  STR(X1, XSP, 8 * 2);
  STR(X0, XSP, 8 * 1);
  SUB(XSP, XSP, stack_size);

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

  ADD(XSP, XSP, stack_size);
  LDR(X0, XSP, 8 * 1);
  LDR(X1, XSP, 8 * 2);
  LDR(X2, XSP, 8 * 3);
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
  // rcx = target function
  // rdx = arg0
  // r8  = arg1
  // r9  = arg2

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
  SUB(XSP, XSP, stack_size);

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
  ADD(XSP, XSP, stack_size);
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
  // ebx = target PPC address
  // rcx = context

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
  SUB(XSP, XSP, stack_size);

  code_offsets.prolog_stack_alloc = offset();
  code_offsets.body = offset();

  // Save volatile registers
  EmitSaveVolatileRegs();

  // mov(rcx, rsi);  // context
  // mov(rdx, rbx);
  // mov(rax, reinterpret_cast<uint64_t>(&ResolveFunction));
  // call(rax)
  MOV(X0, GetContextReg());  // context
  MOV(X1, X1);
  MOVP2R(X16, &ResolveFunction);
  BLR(X16);

  EmitLoadVolatileRegs();

  code_offsets.epilog = offset();

  // add(rsp, stack_size);
  // jmp(rax);
  ADD(XSP, XSP, stack_size);
  BR(X16);

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
  STR(X0, XSP, offsetof(StackLayout::Thunk, r[0]));
  STR(X1, XSP, offsetof(StackLayout::Thunk, r[1]));
  STR(X2, XSP, offsetof(StackLayout::Thunk, r[2]));
  STR(X3, XSP, offsetof(StackLayout::Thunk, r[3]));
  STR(X4, XSP, offsetof(StackLayout::Thunk, r[4]));
  STR(X5, XSP, offsetof(StackLayout::Thunk, r[5]));
  STR(X6, XSP, offsetof(StackLayout::Thunk, r[6]));
  STR(X7, XSP, offsetof(StackLayout::Thunk, r[7]));
  STR(X8, XSP, offsetof(StackLayout::Thunk, r[8]));

  STR(X9, XSP, offsetof(StackLayout::Thunk, r[9]));
  STR(X10, XSP, offsetof(StackLayout::Thunk, r[10]));
  STR(X11, XSP, offsetof(StackLayout::Thunk, r[11]));
  STR(X12, XSP, offsetof(StackLayout::Thunk, r[12]));
  STR(X13, XSP, offsetof(StackLayout::Thunk, r[13]));
  STR(X14, XSP, offsetof(StackLayout::Thunk, r[14]));
  STR(X15, XSP, offsetof(StackLayout::Thunk, r[15]));
  STR(X16, XSP, offsetof(StackLayout::Thunk, r[16]));
  STR(X17, XSP, offsetof(StackLayout::Thunk, r[17]));
  STR(X18, XSP, offsetof(StackLayout::Thunk, r[18]));

  STR(Q0, XSP, offsetof(StackLayout::Thunk, xmm[0]));
  STR(Q1, XSP, offsetof(StackLayout::Thunk, xmm[1]));
  STR(Q2, XSP, offsetof(StackLayout::Thunk, xmm[2]));
  STR(Q3, XSP, offsetof(StackLayout::Thunk, xmm[3]));
  STR(Q4, XSP, offsetof(StackLayout::Thunk, xmm[4]));
  STR(Q5, XSP, offsetof(StackLayout::Thunk, xmm[5]));
  STR(Q6, XSP, offsetof(StackLayout::Thunk, xmm[6]));
  STR(Q7, XSP, offsetof(StackLayout::Thunk, xmm[7]));

  STR(Q8, XSP, offsetof(StackLayout::Thunk, xmm[8]));
  STR(Q9, XSP, offsetof(StackLayout::Thunk, xmm[9]));
  STR(Q10, XSP, offsetof(StackLayout::Thunk, xmm[10]));
  STR(Q11, XSP, offsetof(StackLayout::Thunk, xmm[11]));
  STR(Q12, XSP, offsetof(StackLayout::Thunk, xmm[12]));
  STR(Q13, XSP, offsetof(StackLayout::Thunk, xmm[13]));
  STR(Q14, XSP, offsetof(StackLayout::Thunk, xmm[14]));
  STR(Q15, XSP, offsetof(StackLayout::Thunk, xmm[15]));
  STR(Q16, XSP, offsetof(StackLayout::Thunk, xmm[16]));
  STR(Q17, XSP, offsetof(StackLayout::Thunk, xmm[17]));
  STR(Q18, XSP, offsetof(StackLayout::Thunk, xmm[18]));
  STR(Q19, XSP, offsetof(StackLayout::Thunk, xmm[19]));
  STR(Q20, XSP, offsetof(StackLayout::Thunk, xmm[20]));
  STR(Q21, XSP, offsetof(StackLayout::Thunk, xmm[21]));
  STR(Q22, XSP, offsetof(StackLayout::Thunk, xmm[22]));
  STR(Q23, XSP, offsetof(StackLayout::Thunk, xmm[23]));
  STR(Q24, XSP, offsetof(StackLayout::Thunk, xmm[24]));
  STR(Q25, XSP, offsetof(StackLayout::Thunk, xmm[25]));
  STR(Q26, XSP, offsetof(StackLayout::Thunk, xmm[26]));
  STR(Q27, XSP, offsetof(StackLayout::Thunk, xmm[27]));
  STR(Q28, XSP, offsetof(StackLayout::Thunk, xmm[28]));
  STR(Q29, XSP, offsetof(StackLayout::Thunk, xmm[29]));
  STR(Q30, XSP, offsetof(StackLayout::Thunk, xmm[30]));
  STR(Q31, XSP, offsetof(StackLayout::Thunk, xmm[31]));
}

void A64ThunkEmitter::EmitLoadVolatileRegs() {
  LDR(X0, XSP, offsetof(StackLayout::Thunk, r[0]));
  LDR(X1, XSP, offsetof(StackLayout::Thunk, r[1]));
  LDR(X2, XSP, offsetof(StackLayout::Thunk, r[2]));
  LDR(X3, XSP, offsetof(StackLayout::Thunk, r[3]));
  LDR(X4, XSP, offsetof(StackLayout::Thunk, r[4]));
  LDR(X5, XSP, offsetof(StackLayout::Thunk, r[5]));
  LDR(X6, XSP, offsetof(StackLayout::Thunk, r[6]));
  LDR(X7, XSP, offsetof(StackLayout::Thunk, r[7]));
  LDR(X8, XSP, offsetof(StackLayout::Thunk, r[8]));

  LDR(X9, XSP, offsetof(StackLayout::Thunk, r[9]));
  LDR(X10, XSP, offsetof(StackLayout::Thunk, r[10]));
  LDR(X11, XSP, offsetof(StackLayout::Thunk, r[11]));
  LDR(X12, XSP, offsetof(StackLayout::Thunk, r[12]));
  LDR(X13, XSP, offsetof(StackLayout::Thunk, r[13]));
  LDR(X14, XSP, offsetof(StackLayout::Thunk, r[14]));
  LDR(X15, XSP, offsetof(StackLayout::Thunk, r[15]));
  LDR(X16, XSP, offsetof(StackLayout::Thunk, r[16]));
  LDR(X17, XSP, offsetof(StackLayout::Thunk, r[17]));
  LDR(X18, XSP, offsetof(StackLayout::Thunk, r[18]));

  LDR(Q0, XSP, offsetof(StackLayout::Thunk, xmm[0]));
  LDR(Q1, XSP, offsetof(StackLayout::Thunk, xmm[1]));
  LDR(Q2, XSP, offsetof(StackLayout::Thunk, xmm[2]));
  LDR(Q3, XSP, offsetof(StackLayout::Thunk, xmm[3]));
  LDR(Q4, XSP, offsetof(StackLayout::Thunk, xmm[4]));
  LDR(Q5, XSP, offsetof(StackLayout::Thunk, xmm[5]));
  LDR(Q6, XSP, offsetof(StackLayout::Thunk, xmm[6]));
  LDR(Q7, XSP, offsetof(StackLayout::Thunk, xmm[7]));

  LDR(Q8, XSP, offsetof(StackLayout::Thunk, xmm[8]));
  LDR(Q9, XSP, offsetof(StackLayout::Thunk, xmm[9]));
  LDR(Q10, XSP, offsetof(StackLayout::Thunk, xmm[10]));
  LDR(Q11, XSP, offsetof(StackLayout::Thunk, xmm[11]));
  LDR(Q12, XSP, offsetof(StackLayout::Thunk, xmm[12]));
  LDR(Q13, XSP, offsetof(StackLayout::Thunk, xmm[13]));
  LDR(Q14, XSP, offsetof(StackLayout::Thunk, xmm[14]));
  LDR(Q15, XSP, offsetof(StackLayout::Thunk, xmm[15]));
  LDR(Q16, XSP, offsetof(StackLayout::Thunk, xmm[16]));
  LDR(Q17, XSP, offsetof(StackLayout::Thunk, xmm[17]));
  LDR(Q18, XSP, offsetof(StackLayout::Thunk, xmm[18]));
  LDR(Q19, XSP, offsetof(StackLayout::Thunk, xmm[19]));
  LDR(Q20, XSP, offsetof(StackLayout::Thunk, xmm[20]));
  LDR(Q21, XSP, offsetof(StackLayout::Thunk, xmm[21]));
  LDR(Q22, XSP, offsetof(StackLayout::Thunk, xmm[22]));
  LDR(Q23, XSP, offsetof(StackLayout::Thunk, xmm[23]));
  LDR(Q24, XSP, offsetof(StackLayout::Thunk, xmm[24]));
  LDR(Q25, XSP, offsetof(StackLayout::Thunk, xmm[25]));
  LDR(Q26, XSP, offsetof(StackLayout::Thunk, xmm[26]));
  LDR(Q27, XSP, offsetof(StackLayout::Thunk, xmm[27]));
  LDR(Q28, XSP, offsetof(StackLayout::Thunk, xmm[28]));
  LDR(Q29, XSP, offsetof(StackLayout::Thunk, xmm[29]));
  LDR(Q30, XSP, offsetof(StackLayout::Thunk, xmm[30]));
  LDR(Q31, XSP, offsetof(StackLayout::Thunk, xmm[31]));
}

void A64ThunkEmitter::EmitSaveNonvolatileRegs() {
  STR(X19, XSP, offsetof(StackLayout::Thunk, r[0]));
  STR(X20, XSP, offsetof(StackLayout::Thunk, r[1]));
  STR(X21, XSP, offsetof(StackLayout::Thunk, r[2]));
  STR(X22, XSP, offsetof(StackLayout::Thunk, r[3]));
  STR(X23, XSP, offsetof(StackLayout::Thunk, r[4]));
  STR(X24, XSP, offsetof(StackLayout::Thunk, r[5]));
  STR(X25, XSP, offsetof(StackLayout::Thunk, r[6]));
  STR(X26, XSP, offsetof(StackLayout::Thunk, r[7]));
  STR(X27, XSP, offsetof(StackLayout::Thunk, r[8]));
  STR(X28, XSP, offsetof(StackLayout::Thunk, r[9]));
  STR(X29, XSP, offsetof(StackLayout::Thunk, r[10]));
  STR(X30, XSP, offsetof(StackLayout::Thunk, r[11]));
}

void A64ThunkEmitter::EmitLoadNonvolatileRegs() {
  LDR(X19, XSP, offsetof(StackLayout::Thunk, r[0]));
  LDR(X20, XSP, offsetof(StackLayout::Thunk, r[1]));
  LDR(X21, XSP, offsetof(StackLayout::Thunk, r[2]));
  LDR(X22, XSP, offsetof(StackLayout::Thunk, r[3]));
  LDR(X23, XSP, offsetof(StackLayout::Thunk, r[4]));
  LDR(X24, XSP, offsetof(StackLayout::Thunk, r[5]));
  LDR(X25, XSP, offsetof(StackLayout::Thunk, r[6]));
  LDR(X26, XSP, offsetof(StackLayout::Thunk, r[7]));
  LDR(X27, XSP, offsetof(StackLayout::Thunk, r[8]));
  LDR(X28, XSP, offsetof(StackLayout::Thunk, r[9]));
  LDR(X29, XSP, offsetof(StackLayout::Thunk, r[10]));
  LDR(X30, XSP, offsetof(StackLayout::Thunk, r[11]));
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
