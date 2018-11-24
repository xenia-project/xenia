/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/x64/x64_backend.h"

#include <stddef.h>

#include "third_party/capstone/include/capstone.h"
#include "third_party/capstone/include/x86.h"

#include "xenia/base/exception_handler.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/backend/x64/x64_assembler.h"
#include "xenia/cpu/backend/x64/x64_code_cache.h"
#include "xenia/cpu/backend/x64/x64_emitter.h"
#include "xenia/cpu/backend/x64/x64_function.h"
#include "xenia/cpu/backend/x64/x64_sequences.h"
#include "xenia/cpu/backend/x64/x64_stack_layout.h"
#include "xenia/cpu/breakpoint.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/stack_walker.h"

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

 private:
  // The following four functions provide save/load functionality for registers.
  // They assume at least StackLayout::THUNK_STACK_SIZE bytes have been
  // allocated on the stack.
  void EmitSaveVolatileRegs();
  void EmitLoadVolatileRegs();
  void EmitSaveNonvolatileRegs();
  void EmitLoadNonvolatileRegs();
};

X64Backend::X64Backend() : Backend(), code_cache_(nullptr) {
  if (cs_open(CS_ARCH_X86, CS_MODE_64, &capstone_handle_) != CS_ERR_OK) {
    assert_always("Failed to initialize capstone");
  }
  cs_option(capstone_handle_, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
  cs_option(capstone_handle_, CS_OPT_DETAIL, CS_OPT_ON);
  cs_option(capstone_handle_, CS_OPT_SKIPDATA, CS_OPT_OFF);
}

X64Backend::~X64Backend() {
  if (capstone_handle_) {
    cs_close(&capstone_handle_);
  }

  X64Emitter::FreeConstData(emitter_data_);
  ExceptionHandler::Uninstall(&ExceptionCallbackThunk, this);
}

bool X64Backend::Initialize(Processor* processor) {
  if (!Backend::Initialize(processor)) {
    return false;
  }

  Xbyak::util::Cpu cpu;
  if (!cpu.has(Xbyak::util::Cpu::tAVX)) {
    XELOGE("This CPU does not support AVX. The emulator will now crash.");
    return false;
  }

  // Need movbe to do advanced LOAD/STORE tricks.
  if (FLAGS_enable_haswell_instructions) {
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
  emitter_data_ = X64Emitter::PlaceConstData();

  // Setup exception callback
  ExceptionHandler::Install(&ExceptionCallbackThunk, this);

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

uint64_t ReadCapstoneReg(X64Context* context, x86_reg reg) {
  switch (reg) {
    case X86_REG_RAX:
      return context->rax;
    case X86_REG_RCX:
      return context->rcx;
    case X86_REG_RDX:
      return context->rdx;
    case X86_REG_RBX:
      return context->rbx;
    case X86_REG_RSP:
      return context->rsp;
    case X86_REG_RBP:
      return context->rbp;
    case X86_REG_RSI:
      return context->rsi;
    case X86_REG_RDI:
      return context->rdi;
    case X86_REG_R8:
      return context->r8;
    case X86_REG_R9:
      return context->r9;
    case X86_REG_R10:
      return context->r10;
    case X86_REG_R11:
      return context->r11;
    case X86_REG_R12:
      return context->r12;
    case X86_REG_R13:
      return context->r13;
    case X86_REG_R14:
      return context->r14;
    case X86_REG_R15:
      return context->r15;
    default:
      assert_unhandled_case(reg);
      return 0;
  }
}

#define X86_EFLAGS_CF 0x00000001  // Carry Flag
#define X86_EFLAGS_PF 0x00000004  // Parity Flag
#define X86_EFLAGS_ZF 0x00000040  // Zero Flag
#define X86_EFLAGS_SF 0x00000080  // Sign Flag
#define X86_EFLAGS_OF 0x00000800  // Overflow Flag
bool TestCapstoneEflags(uint32_t eflags, uint32_t insn) {
  // https://www.felixcloutier.com/x86/Jcc.html
  switch (insn) {
    case X86_INS_JAE:
      // CF=0 && ZF=0
      return ((eflags & X86_EFLAGS_CF) == 0) && ((eflags & X86_EFLAGS_ZF) == 0);
    case X86_INS_JA:
      // CF=0
      return (eflags & X86_EFLAGS_CF) == 0;
    case X86_INS_JBE:
      // CF=1 || ZF=1
      return ((eflags & X86_EFLAGS_CF) == X86_EFLAGS_CF) ||
             ((eflags & X86_EFLAGS_ZF) == X86_EFLAGS_ZF);
    case X86_INS_JB:
      // CF=1
      return (eflags & X86_EFLAGS_CF) == X86_EFLAGS_CF;
    case X86_INS_JE:
      // ZF=1
      return (eflags & X86_EFLAGS_ZF) == X86_EFLAGS_ZF;
    case X86_INS_JGE:
      // SF=OF
      return (eflags & X86_EFLAGS_SF) == (eflags & X86_EFLAGS_OF);
    case X86_INS_JG:
      // ZF=0 && SF=OF
      return ((eflags & X86_EFLAGS_ZF) == 0) &&
             ((eflags & X86_EFLAGS_SF) == (eflags & X86_EFLAGS_OF));
    case X86_INS_JLE:
      // ZF=1 || SF!=OF
      return ((eflags & X86_EFLAGS_ZF) == X86_EFLAGS_ZF) ||
             ((eflags & X86_EFLAGS_SF) != X86_EFLAGS_OF);
    case X86_INS_JL:
      // SF!=OF
      return (eflags & X86_EFLAGS_SF) != (eflags & X86_EFLAGS_OF);
    case X86_INS_JNE:
      // ZF=0
      return (eflags & X86_EFLAGS_ZF) == 0;
    case X86_INS_JNO:
      // OF=0
      return (eflags & X86_EFLAGS_OF) == 0;
    case X86_INS_JNP:
      // PF=0
      return (eflags & X86_EFLAGS_PF) == 0;
    case X86_INS_JNS:
      // SF=0
      return (eflags & X86_EFLAGS_SF) == 0;
    case X86_INS_JO:
      // OF=1
      return (eflags & X86_EFLAGS_OF) == X86_EFLAGS_OF;
    case X86_INS_JP:
      // PF=1
      return (eflags & X86_EFLAGS_PF) == X86_EFLAGS_PF;
    case X86_INS_JS:
      // SF=1
      return (eflags & X86_EFLAGS_SF) == X86_EFLAGS_SF;
    default:
      assert_unhandled_case(insn);
      return false;
  }
}

uint64_t X64Backend::CalculateNextHostInstruction(ThreadDebugInfo* thread_info,
                                                  uint64_t current_pc) {
  auto machine_code_ptr = reinterpret_cast<const uint8_t*>(current_pc);
  size_t remaining_machine_code_size = 64;
  uint64_t host_address = current_pc;
  cs_insn insn = {0};
  cs_detail all_detail = {0};
  insn.detail = &all_detail;
  cs_disasm_iter(capstone_handle_, &machine_code_ptr,
                 &remaining_machine_code_size, &host_address, &insn);
  auto& detail = all_detail.x86;
  switch (insn.id) {
    default:
      // Not a branching instruction - just move over it.
      return current_pc + insn.size;
    case X86_INS_CALL: {
      assert_true(detail.op_count == 1);
      assert_true(detail.operands[0].type == X86_OP_REG);
      uint64_t target_pc =
          ReadCapstoneReg(&thread_info->host_context, detail.operands[0].reg);
      return target_pc;
    } break;
    case X86_INS_RET: {
      assert_zero(detail.op_count);
      auto stack_ptr =
          reinterpret_cast<uint64_t*>(thread_info->host_context.rsp);
      uint64_t target_pc = stack_ptr[0];
      return target_pc;
    } break;
    case X86_INS_JMP: {
      assert_true(detail.op_count == 1);
      if (detail.operands[0].type == X86_OP_IMM) {
        uint64_t target_pc = static_cast<uint64_t>(detail.operands[0].imm);
        return target_pc;
      } else if (detail.operands[0].type == X86_OP_REG) {
        uint64_t target_pc =
            ReadCapstoneReg(&thread_info->host_context, detail.operands[0].reg);
        return target_pc;
      } else {
        // TODO(benvanik): find some more uses of this.
        assert_always("jmp branch emulation not yet implemented");
        return current_pc + insn.size;
      }
    } break;
    case X86_INS_JCXZ:
    case X86_INS_JECXZ:
    case X86_INS_JRCXZ:
      assert_always("j*cxz branch emulation not yet implemented");
      return current_pc + insn.size;
    case X86_INS_JAE:
    case X86_INS_JA:
    case X86_INS_JBE:
    case X86_INS_JB:
    case X86_INS_JE:
    case X86_INS_JGE:
    case X86_INS_JG:
    case X86_INS_JLE:
    case X86_INS_JL:
    case X86_INS_JNE:
    case X86_INS_JNO:
    case X86_INS_JNP:
    case X86_INS_JNS:
    case X86_INS_JO:
    case X86_INS_JP:
    case X86_INS_JS: {
      assert_true(detail.op_count == 1);
      assert_true(detail.operands[0].type == X86_OP_IMM);
      uint64_t target_pc = static_cast<uint64_t>(detail.operands[0].imm);
      bool test_passed =
          TestCapstoneEflags(thread_info->host_context.eflags, insn.id);
      if (test_passed) {
        return target_pc;
      } else {
        return current_pc + insn.size;
      }
    } break;
  }
}

void X64Backend::InstallBreakpoint(Breakpoint* breakpoint) {
  breakpoint->ForEachHostAddress([breakpoint](uint64_t host_address) {
    auto ptr = reinterpret_cast<void*>(host_address);
    auto original_bytes = xe::load_and_swap<uint16_t>(ptr);
    assert_true(original_bytes != 0x0F0B);
    xe::store_and_swap<uint16_t>(ptr, 0x0F0B);
    breakpoint->backend_data().emplace_back(host_address, original_bytes);
  });
}

void X64Backend::InstallBreakpoint(Breakpoint* breakpoint, Function* fn) {
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

void X64Backend::UninstallBreakpoint(Breakpoint* breakpoint) {
  for (auto& pair : breakpoint->backend_data()) {
    auto ptr = reinterpret_cast<uint8_t*>(pair.first);
    auto instruction_bytes = xe::load_and_swap<uint16_t>(ptr);
    assert_true(instruction_bytes == 0x0F0B);
    xe::store_and_swap<uint16_t>(ptr, static_cast<uint16_t>(pair.second));
  }
  breakpoint->backend_data().clear();
}

bool X64Backend::ExceptionCallbackThunk(Exception* ex, void* data) {
  auto backend = reinterpret_cast<X64Backend*>(data);
  return backend->ExceptionCallback(ex);
}

bool X64Backend::ExceptionCallback(Exception* ex) {
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

X64ThunkEmitter::X64ThunkEmitter(X64Backend* backend, XbyakAllocator* allocator)
    : X64Emitter(backend, allocator) {}

X64ThunkEmitter::~X64ThunkEmitter() {}

HostToGuestThunk X64ThunkEmitter::EmitHostToGuestThunk() {
  // rcx = target
  // rdx = arg0 (context)
  // r8 = arg1 (guest return address)

  const size_t stack_size = StackLayout::THUNK_STACK_SIZE;
  // rsp + 0 = return address
  mov(qword[rsp + 8 * 3], r8);
  mov(qword[rsp + 8 * 2], rdx);
  mov(qword[rsp + 8 * 1], rcx);
  sub(rsp, stack_size);

  // Save nonvolatile registers.
  EmitSaveNonvolatileRegs();

  mov(rax, rcx);
  mov(rsi, rdx);  // context
  mov(rcx, r8);   // return address
  call(rax);

  EmitLoadNonvolatileRegs();

  add(rsp, stack_size);
  mov(rcx, qword[rsp + 8 * 1]);
  mov(rdx, qword[rsp + 8 * 2]);
  mov(r8, qword[rsp + 8 * 3]);
  ret();

  void* fn = Emplace(stack_size);
  return (HostToGuestThunk)fn;
}

GuestToHostThunk X64ThunkEmitter::EmitGuestToHostThunk() {
  // rcx = target function
  // rdx = arg0
  // r8  = arg1
  // r9  = arg2

  const size_t stack_size = StackLayout::THUNK_STACK_SIZE;
  // rsp + 0 = return address
  sub(rsp, stack_size);

  // Save off volatile registers.
  EmitSaveVolatileRegs();

  mov(rax, rcx);              // function
  mov(rcx, GetContextReg());  // context
  call(rax);

  EmitLoadVolatileRegs();

  add(rsp, stack_size);
  ret();

  void* fn = Emplace(stack_size);
  return (GuestToHostThunk)fn;
}

// X64Emitter handles actually resolving functions.
extern "C" uint64_t ResolveFunction(void* raw_context, uint32_t target_address);

ResolveFunctionThunk X64ThunkEmitter::EmitResolveFunctionThunk() {
  // ebx = target PPC address
  // rcx = context
  const size_t stack_size = StackLayout::THUNK_STACK_SIZE;

  // rsp + 0 = return address
  sub(rsp, stack_size);

  // Save volatile registers
  EmitSaveVolatileRegs();

  mov(rcx, rsi);  // context
  mov(rdx, rbx);
  mov(rax, uint64_t(&ResolveFunction));
  call(rax);

  EmitLoadVolatileRegs();

  add(rsp, stack_size);
  jmp(rax);

  void* fn = Emplace(stack_size);
  return (ResolveFunctionThunk)fn;
}

void X64ThunkEmitter::EmitSaveVolatileRegs() {
  // Save off volatile registers.
  // mov(qword[rsp + offsetof(StackLayout::Thunk, r[0])], rax);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[1])], rcx);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[2])], rdx);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[3])], r8);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[4])], r9);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[5])], r10);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[6])], r11);

  // movaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[0])], xmm0);
  movaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[1])], xmm1);
  movaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[2])], xmm2);
  movaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[3])], xmm3);
  movaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[4])], xmm4);
  movaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[5])], xmm5);
}

void X64ThunkEmitter::EmitLoadVolatileRegs() {
  // Load volatile registers from our stack frame.
  // movaps(xmm0, qword[rsp + offsetof(StackLayout::Thunk, xmm[0])]);
  movaps(xmm1, qword[rsp + offsetof(StackLayout::Thunk, xmm[1])]);
  movaps(xmm2, qword[rsp + offsetof(StackLayout::Thunk, xmm[2])]);
  movaps(xmm3, qword[rsp + offsetof(StackLayout::Thunk, xmm[3])]);
  movaps(xmm4, qword[rsp + offsetof(StackLayout::Thunk, xmm[4])]);
  movaps(xmm5, qword[rsp + offsetof(StackLayout::Thunk, xmm[5])]);

  // mov(rax, qword[rsp + offsetof(StackLayout::Thunk, r[0])]);
  mov(rcx, qword[rsp + offsetof(StackLayout::Thunk, r[1])]);
  mov(rdx, qword[rsp + offsetof(StackLayout::Thunk, r[2])]);
  mov(r8, qword[rsp + offsetof(StackLayout::Thunk, r[3])]);
  mov(r9, qword[rsp + offsetof(StackLayout::Thunk, r[4])]);
  mov(r10, qword[rsp + offsetof(StackLayout::Thunk, r[5])]);
  mov(r11, qword[rsp + offsetof(StackLayout::Thunk, r[6])]);
}

void X64ThunkEmitter::EmitSaveNonvolatileRegs() {
  // Preserve nonvolatile registers.
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[0])], rbx);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[1])], rcx);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[2])], rbp);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[3])], rsi);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[4])], rdi);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[5])], r12);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[6])], r13);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[7])], r14);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[8])], r15);

  movaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[0])], xmm6);
  movaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[1])], xmm7);
  movaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[2])], xmm8);
  movaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[3])], xmm9);
  movaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[4])], xmm10);
  movaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[5])], xmm11);
  movaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[6])], xmm12);
  movaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[7])], xmm13);
  movaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[8])], xmm14);
  movaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[9])], xmm15);
}

void X64ThunkEmitter::EmitLoadNonvolatileRegs() {
  movaps(xmm6, qword[rsp + offsetof(StackLayout::Thunk, xmm[0])]);
  movaps(xmm7, qword[rsp + offsetof(StackLayout::Thunk, xmm[1])]);
  movaps(xmm8, qword[rsp + offsetof(StackLayout::Thunk, xmm[2])]);
  movaps(xmm9, qword[rsp + offsetof(StackLayout::Thunk, xmm[3])]);
  movaps(xmm10, qword[rsp + offsetof(StackLayout::Thunk, xmm[4])]);
  movaps(xmm11, qword[rsp + offsetof(StackLayout::Thunk, xmm[5])]);
  movaps(xmm12, qword[rsp + offsetof(StackLayout::Thunk, xmm[6])]);
  movaps(xmm13, qword[rsp + offsetof(StackLayout::Thunk, xmm[7])]);
  movaps(xmm14, qword[rsp + offsetof(StackLayout::Thunk, xmm[8])]);
  movaps(xmm15, qword[rsp + offsetof(StackLayout::Thunk, xmm[9])]);

  mov(rbx, qword[rsp + offsetof(StackLayout::Thunk, r[0])]);
  mov(rcx, qword[rsp + offsetof(StackLayout::Thunk, r[1])]);
  mov(rbp, qword[rsp + offsetof(StackLayout::Thunk, r[2])]);
  mov(rsi, qword[rsp + offsetof(StackLayout::Thunk, r[3])]);
  mov(rdi, qword[rsp + offsetof(StackLayout::Thunk, r[4])]);
  mov(r12, qword[rsp + offsetof(StackLayout::Thunk, r[5])]);
  mov(r13, qword[rsp + offsetof(StackLayout::Thunk, r[6])]);
  mov(r14, qword[rsp + offsetof(StackLayout::Thunk, r[7])]);
  mov(r15, qword[rsp + offsetof(StackLayout::Thunk, r[8])]);
}

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
