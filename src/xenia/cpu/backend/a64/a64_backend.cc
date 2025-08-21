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

#include "third_party/capstone/include/capstone/arm64.h"
#include "third_party/capstone/include/capstone/capstone.h"

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
             "    0 = armv8.0\n"
             "    1 = LSE\n"
             "    2 = F16C\n"
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
  // x0, v0 is not saved for use as arg0/return
  // x1-x15, x30 | v0-v7 and v16-v31
  void EmitSaveVolatileRegs();
  void EmitLoadVolatileRegs();

  // Callee saved:
  // Subroutines must preserve these registers if they intend to use them
  // x19-x30 | d8-d15
  void EmitSaveNonvolatileRegs();
  void EmitLoadNonvolatileRegs();
};

A64Backend::A64Backend() : Backend(), code_cache_(nullptr) {
  if (cs_open(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &capstone_handle_) !=
      CS_ERR_OK) {
    assert_always("Failed to initialize capstone");
  }
  cs_option(capstone_handle_, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
  cs_option(capstone_handle_, CS_OPT_DETAIL, CS_OPT_ON);
  cs_option(capstone_handle_, CS_OPT_SKIPDATA, CS_OPT_OFF);
}

A64Backend::~A64Backend() {
  if (capstone_handle_) {
    cs_close(&capstone_handle_);
  }

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

uint64_t ReadCapstoneReg(HostThreadContext* context, arm64_reg reg) {
  switch (reg) {
    case ARM64_REG_X0:
      return context->x[0];
    case ARM64_REG_X1:
      return context->x[1];
    case ARM64_REG_X2:
      return context->x[2];
    case ARM64_REG_X3:
      return context->x[3];
    case ARM64_REG_X4:
      return context->x[4];
    case ARM64_REG_X5:
      return context->x[5];
    case ARM64_REG_X6:
      return context->x[6];
    case ARM64_REG_X7:
      return context->x[7];
    case ARM64_REG_X8:
      return context->x[8];
    case ARM64_REG_X9:
      return context->x[9];
    case ARM64_REG_X10:
      return context->x[10];
    case ARM64_REG_X11:
      return context->x[11];
    case ARM64_REG_X12:
      return context->x[12];
    case ARM64_REG_X13:
      return context->x[13];
    case ARM64_REG_X14:
      return context->x[14];
    case ARM64_REG_X15:
      return context->x[15];
    case ARM64_REG_X16:
      return context->x[16];
    case ARM64_REG_X17:
      return context->x[17];
    case ARM64_REG_X18:
      return context->x[18];
    case ARM64_REG_X19:
      return context->x[19];
    case ARM64_REG_X20:
      return context->x[20];
    case ARM64_REG_X21:
      return context->x[21];
    case ARM64_REG_X22:
      return context->x[22];
    case ARM64_REG_X23:
      return context->x[23];
    case ARM64_REG_X24:
      return context->x[24];
    case ARM64_REG_X25:
      return context->x[25];
    case ARM64_REG_X26:
      return context->x[26];
    case ARM64_REG_X27:
      return context->x[27];
    case ARM64_REG_X28:
      return context->x[28];
    case ARM64_REG_X29:
      return context->x[29];
    case ARM64_REG_X30:
      return context->x[30];
    case ARM64_REG_W0:
      return uint32_t(context->x[0]);
    case ARM64_REG_W1:
      return uint32_t(context->x[1]);
    case ARM64_REG_W2:
      return uint32_t(context->x[2]);
    case ARM64_REG_W3:
      return uint32_t(context->x[3]);
    case ARM64_REG_W4:
      return uint32_t(context->x[4]);
    case ARM64_REG_W5:
      return uint32_t(context->x[5]);
    case ARM64_REG_W6:
      return uint32_t(context->x[6]);
    case ARM64_REG_W7:
      return uint32_t(context->x[7]);
    case ARM64_REG_W8:
      return uint32_t(context->x[8]);
    case ARM64_REG_W9:
      return uint32_t(context->x[9]);
    case ARM64_REG_W10:
      return uint32_t(context->x[10]);
    case ARM64_REG_W11:
      return uint32_t(context->x[11]);
    case ARM64_REG_W12:
      return uint32_t(context->x[12]);
    case ARM64_REG_W13:
      return uint32_t(context->x[13]);
    case ARM64_REG_W14:
      return uint32_t(context->x[14]);
    case ARM64_REG_W15:
      return uint32_t(context->x[15]);
    case ARM64_REG_W16:
      return uint32_t(context->x[16]);
    case ARM64_REG_W17:
      return uint32_t(context->x[17]);
    case ARM64_REG_W18:
      return uint32_t(context->x[18]);
    case ARM64_REG_W19:
      return uint32_t(context->x[19]);
    case ARM64_REG_W20:
      return uint32_t(context->x[20]);
    case ARM64_REG_W21:
      return uint32_t(context->x[21]);
    case ARM64_REG_W22:
      return uint32_t(context->x[22]);
    case ARM64_REG_W23:
      return uint32_t(context->x[23]);
    case ARM64_REG_W24:
      return uint32_t(context->x[24]);
    case ARM64_REG_W25:
      return uint32_t(context->x[25]);
    case ARM64_REG_W26:
      return uint32_t(context->x[26]);
    case ARM64_REG_W27:
      return uint32_t(context->x[27]);
    case ARM64_REG_W28:
      return uint32_t(context->x[28]);
    case ARM64_REG_W29:
      return uint32_t(context->x[29]);
    case ARM64_REG_W30:
      return uint32_t(context->x[30]);
    default:
      assert_unhandled_case(reg);
      return 0;
  }
}

bool TestCapstonePstate(arm64_cc cond, uint32_t pstate) {
  // https://devblogs.microsoft.com/oldnewthing/20220815-00/?p=106975
  // Upper 4 bits of pstate are NZCV
  const bool N = !!(pstate & 0x80000000);
  const bool Z = !!(pstate & 0x40000000);
  const bool C = !!(pstate & 0x20000000);
  const bool V = !!(pstate & 0x10000000);
  switch (cond) {
    case ARM64_CC_EQ:
      return (Z == true);
    case ARM64_CC_NE:
      return (Z == false);
    case ARM64_CC_HS:
      return (C == true);
    case ARM64_CC_LO:
      return (C == false);
    case ARM64_CC_MI:
      return (N == true);
    case ARM64_CC_PL:
      return (N == false);
    case ARM64_CC_VS:
      return (V == true);
    case ARM64_CC_VC:
      return (V == false);
    case ARM64_CC_HI:
      return ((C == true) && (Z == false));
    case ARM64_CC_LS:
      return ((C == false) || (Z == true));
    case ARM64_CC_GE:
      return (N == V);
    case ARM64_CC_LT:
      return (N != V);
    case ARM64_CC_GT:
      return ((Z == false) && (N == V));
    case ARM64_CC_LE:
      return ((Z == true) || (N != V));
    case ARM64_CC_AL:
      return true;
    case ARM64_CC_NV:
      return false;
    default:
      assert_unhandled_case(cond);
      return false;
  }
}

uint64_t A64Backend::CalculateNextHostInstruction(ThreadDebugInfo* thread_info,
                                                  uint64_t current_pc) {
  auto machine_code_ptr = reinterpret_cast<const uint8_t*>(current_pc);
  size_t remaining_machine_code_size = 64;
  uint64_t host_address = current_pc;
  cs_insn insn = {0};
  cs_detail all_detail = {0};
  insn.detail = &all_detail;
  cs_disasm_iter(capstone_handle_, &machine_code_ptr,
                 &remaining_machine_code_size, &host_address, &insn);
  const auto& detail = all_detail.arm64;
  switch (insn.id) {
    case ARM64_INS_B:
    case ARM64_INS_BL: {
      assert_true(detail.operands[0].type == ARM64_OP_IMM);
      const int64_t pc_offset = static_cast<int64_t>(detail.operands[0].imm);
      const bool test_passed =
          TestCapstonePstate(detail.cc, thread_info->host_context.cpsr);
      if (test_passed) {
        return current_pc + pc_offset;
      } else {
        return current_pc + insn.size;
      }
    } break;
    case ARM64_INS_BR:
    case ARM64_INS_BLR: {
      assert_true(detail.operands[0].type == ARM64_OP_REG);
      const uint64_t target_pc =
          ReadCapstoneReg(&thread_info->host_context, detail.operands[0].reg);
      return target_pc;
    } break;
    case ARM64_INS_RET: {
      assert_true(detail.operands[0].type == ARM64_OP_REG);
      const uint64_t target_pc =
          ReadCapstoneReg(&thread_info->host_context, detail.operands[0].reg);
      return target_pc;
    } break;
    case ARM64_INS_CBNZ: {
      assert_true(detail.operands[0].type == ARM64_OP_REG);
      assert_true(detail.operands[1].type == ARM64_OP_IMM);
      const int64_t pc_offset = static_cast<int64_t>(detail.operands[1].imm);
      const bool test_passed = (0 != ReadCapstoneReg(&thread_info->host_context,
                                                     detail.operands[0].reg));
      if (test_passed) {
        return current_pc + pc_offset;
      } else {
        return current_pc + insn.size;
      }
    } break;
    case ARM64_INS_CBZ: {
      assert_true(detail.operands[0].type == ARM64_OP_REG);
      assert_true(detail.operands[1].type == ARM64_OP_IMM);
      const int64_t pc_offset = static_cast<int64_t>(detail.operands[1].imm);
      const bool test_passed = (0 == ReadCapstoneReg(&thread_info->host_context,
                                                     detail.operands[0].reg));
      if (test_passed) {
        return current_pc + pc_offset;
      } else {
        return current_pc + insn.size;
      }
    } break;
    default: {
      // Not a branching instruction - just move over it.
      return current_pc + insn.size;
    } break;
  }
}

void A64Backend::InstallBreakpoint(Breakpoint* breakpoint) {
  breakpoint->ForEachHostAddress([breakpoint](uint64_t host_address) {
    auto ptr = reinterpret_cast<void*>(host_address);
    auto original_bytes = xe::load_and_swap<uint32_t>(ptr);
    assert_true(original_bytes != 0x0000'dead);
    xe::store_and_swap<uint32_t>(ptr, 0x0000'dead);
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
  auto original_bytes = xe::load_and_swap<uint32_t>(ptr);
  assert_true(original_bytes != 0x0000'dead);
  xe::store_and_swap<uint32_t>(ptr, 0x0000'dead);
  breakpoint->backend_data().emplace_back(host_address, original_bytes);
}

void A64Backend::UninstallBreakpoint(Breakpoint* breakpoint) {
  for (auto& pair : breakpoint->backend_data()) {
    auto ptr = reinterpret_cast<uint8_t*>(pair.first);
    auto instruction_bytes = xe::load_and_swap<uint32_t>(ptr);
    assert_true(instruction_bytes == 0x0000'dead);
    xe::store_and_swap<uint32_t>(ptr, static_cast<uint32_t>(pair.second));
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
      xe::load_and_swap<uint32_t>(reinterpret_cast<void*>(ex->pc()));
  if (instruction_bytes != 0x0000'dead) {
    // Not our `udf #0xdead` - not us.
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

  SUB(SP, SP, stack_size);

  code_offsets.prolog_stack_alloc = offset();
  code_offsets.body = offset();

  EmitSaveNonvolatileRegs();

  MOV(X16, X0);
  MOV(GetContextReg(), X1);  // context
  MOV(X0, X2);               // return address
  BLR(X16);

  EmitLoadNonvolatileRegs();

  code_offsets.epilog = offset();

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

  SUB(SP, SP, stack_size);

  code_offsets.prolog_stack_alloc = offset();
  code_offsets.body = offset();

  EmitSaveVolatileRegs();

  MOV(X16, X0);              // function
  MOV(X0, GetContextReg());  // context
  BLR(X16);

  EmitLoadVolatileRegs();

  code_offsets.epilog = offset();

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
  // W17 = target PPC address
  // X0 = context

  struct _code_offsets {
    size_t prolog;
    size_t prolog_stack_alloc;
    size_t body;
    size_t epilog;
    size_t tail;
  } code_offsets = {};

  const size_t stack_size = StackLayout::THUNK_STACK_SIZE;

  code_offsets.prolog = offset();

  // Preserve context register
  STP(ZR, X0, SP, PRE_INDEXED, -16);

  SUB(SP, SP, stack_size);

  code_offsets.prolog_stack_alloc = offset();
  code_offsets.body = offset();

  EmitSaveVolatileRegs();

  // mov(rcx, rsi);  // context
  // mov(rdx, rbx);
  // mov(rax, reinterpret_cast<uint64_t>(&ResolveFunction));
  // call(rax)
  MOV(X0, GetContextReg());  // context
  MOV(W1, W17);
  MOV(X16, reinterpret_cast<uint64_t>(&ResolveFunction));
  BLR(X16);
  MOV(X16, X0);

  EmitLoadVolatileRegs();

  code_offsets.epilog = offset();

  // add(rsp, stack_size);
  // jmp(rax);
  ADD(SP, SP, stack_size);

  // Reload context register
  LDP(ZR, X0, SP, POST_INDEXED, 16);
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

  STR(X17, SP, offsetof(StackLayout::Thunk, r[12]));

  STP(D8, D9, SP, offsetof(StackLayout::Thunk, xmm[0]));
  STP(D10, D11, SP, offsetof(StackLayout::Thunk, xmm[1]));
  STP(D12, D13, SP, offsetof(StackLayout::Thunk, xmm[2]));
  STP(D14, D15, SP, offsetof(StackLayout::Thunk, xmm[3]));
}

void A64ThunkEmitter::EmitLoadNonvolatileRegs() {
  LDP(X19, X20, SP, offsetof(StackLayout::Thunk, r[0]));
  LDP(X21, X22, SP, offsetof(StackLayout::Thunk, r[2]));
  LDP(X23, X24, SP, offsetof(StackLayout::Thunk, r[4]));
  LDP(X25, X26, SP, offsetof(StackLayout::Thunk, r[6]));
  LDP(X27, X28, SP, offsetof(StackLayout::Thunk, r[8]));
  LDP(X29, X30, SP, offsetof(StackLayout::Thunk, r[10]));

  LDR(X17, SP, offsetof(StackLayout::Thunk, r[12]));

  LDP(D8, D9, SP, offsetof(StackLayout::Thunk, xmm[0]));
  LDP(D10, D11, SP, offsetof(StackLayout::Thunk, xmm[1]));
  LDP(D12, D13, SP, offsetof(StackLayout::Thunk, xmm[2]));
  LDP(D14, D15, SP, offsetof(StackLayout::Thunk, xmm[3]));
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
