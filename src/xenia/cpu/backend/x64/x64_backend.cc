/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/x64/x64_backend.h"

#include <stddef.h>
#include <algorithm>
#include "third_party/capstone/include/capstone/capstone.h"
#include "third_party/capstone/include/capstone/x86.h"

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
#include "xenia/cpu/xex_module.h"

DEFINE_bool(record_mmio_access_exceptions, true,
            "For guest addresses records whether we caught any mmio accesses "
            "for them. This info can then be used on a subsequent run to "
            "instruct the recompiler to emit checks",
            "x64");

DEFINE_int64(max_stackpoints, 65536,
             "Max number of host->guest stack mappings we can record.", "x64");

DEFINE_bool(enable_host_guest_stack_synchronization, true,
            "Records entries for guest/host stack mappings at function starts "
            "and checks for reentry at return sites. Has slight performance "
            "impact, but fixes crashes in games that use setjmp/longjmp.",
            "x64");
#if XE_X64_PROFILER_AVAILABLE == 1
DECLARE_bool(instrument_call_times);
#endif

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

class X64HelperEmitter : public X64Emitter {
 public:
  struct _code_offsets {
    size_t prolog;
    size_t prolog_stack_alloc;
    size_t body;
    size_t epilog;
    size_t tail;
  };
  X64HelperEmitter(X64Backend* backend, XbyakAllocator* allocator);
  ~X64HelperEmitter() override;
  HostToGuestThunk EmitHostToGuestThunk();
  GuestToHostThunk EmitGuestToHostThunk();
  ResolveFunctionThunk EmitResolveFunctionThunk();
  void* EmitGuestAndHostSynchronizeStackHelper();
  // 1 for loading byte, 2 for halfword and 4 for word.
  // these specialized versions save space in the caller
  void* EmitGuestAndHostSynchronizeStackSizeLoadThunk(
      void* sync_func, unsigned stack_element_size);

 private:
  void* EmitCurrentForOffsets(const _code_offsets& offsets,
                              size_t stack_size = 0);
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

static void ForwardMMIOAccessForRecording(void* context, void* hostaddr) {
  reinterpret_cast<X64Backend*>(context)
      ->RecordMMIOExceptionForGuestInstruction(hostaddr);
}
#if XE_X64_PROFILER_AVAILABLE == 1
// todo: better way of passing to atexit. maybe do in destructor instead?
// nope, destructor is never called
static GuestProfilerData* backend_profiler_data = nullptr;

static uint64_t nanosecond_lifetime_start = 0;
static void WriteGuestProfilerData() {
  if (cvars::instrument_call_times) {
    uint64_t end = Clock::QueryHostSystemTime();

    uint64_t total = end - nanosecond_lifetime_start;

    double totaltime_divisor = static_cast<double>(total);

    FILE* output_file = nullptr;
    std::vector<std::pair<uint32_t, uint64_t>> unsorted_profile{};
    for (auto&& entry : *backend_profiler_data) {
      if (entry.second) {  // skip times of 0
        unsorted_profile.emplace_back(entry.first, entry.second);
      }
    }

    std::sort(unsorted_profile.begin(), unsorted_profile.end(),
              [](auto& x, auto& y) { return x.second < y.second; });

    fopen_s(&output_file, "profile_times.txt", "w");
    FILE* idapy_file = nullptr;
    fopen_s(&idapy_file, "profile_print_times.py", "w");

    for (auto&& sorted_entry : unsorted_profile) {
      // double time_in_seconds =
      //    static_cast<double>(sorted_entry.second) / 10000000.0;
      double time_in_milliseconds =
          static_cast<double>(sorted_entry.second) / (10000000.0 / 1000.0);

      double slice = static_cast<double>(sorted_entry.second) /
                     static_cast<double>(totaltime_divisor);

      fprintf(output_file,
              "%X took %.20f milliseconds, totaltime slice percentage %.20f \n",
              sorted_entry.first, time_in_milliseconds, slice);

      fprintf(idapy_file,
              "print(get_name(0x%X) + ' took %.20f ms, %.20f percent')\n",
              sorted_entry.first, time_in_milliseconds, slice);
    }

    fclose(output_file);
    fclose(idapy_file);
  }
}

static void GuestProfilerUpdateThreadProc() {
  nanosecond_lifetime_start = Clock::QueryHostSystemTime();

  do {
    xe::threading::Sleep(std::chrono::seconds(30));
    WriteGuestProfilerData();
  } while (true);
}
static std::unique_ptr<xe::threading::Thread> g_profiler_update_thread{};
#endif

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
  if (cvars::x64_extension_mask & kX64EmitMovbe) {
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
  X64HelperEmitter thunk_emitter(this, &allocator);
  host_to_guest_thunk_ = thunk_emitter.EmitHostToGuestThunk();
  guest_to_host_thunk_ = thunk_emitter.EmitGuestToHostThunk();
  resolve_function_thunk_ = thunk_emitter.EmitResolveFunctionThunk();

  if (cvars::enable_host_guest_stack_synchronization) {
    synchronize_guest_and_host_stack_helper_ =
        thunk_emitter.EmitGuestAndHostSynchronizeStackHelper();

    synchronize_guest_and_host_stack_helper_size8_ =
        thunk_emitter.EmitGuestAndHostSynchronizeStackSizeLoadThunk(
            synchronize_guest_and_host_stack_helper_, 1);
    synchronize_guest_and_host_stack_helper_size16_ =
        thunk_emitter.EmitGuestAndHostSynchronizeStackSizeLoadThunk(
            synchronize_guest_and_host_stack_helper_, 2);
    synchronize_guest_and_host_stack_helper_size32_ =
        thunk_emitter.EmitGuestAndHostSynchronizeStackSizeLoadThunk(
            synchronize_guest_and_host_stack_helper_, 4);
  }

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
  if (cvars::record_mmio_access_exceptions) {
    processor->memory()->SetMMIOExceptionRecordingCallback(
        ForwardMMIOAccessForRecording, (void*)this);
  }

#if XE_X64_PROFILER_AVAILABLE == 1
  if (cvars::instrument_call_times) {
    backend_profiler_data = &profiler_data_;
    xe::threading::Thread::CreationParameters slimparams;

    slimparams.create_suspended = false;
    slimparams.initial_priority = xe::threading::ThreadPriority::kLowest;
    slimparams.stack_size = 65536 * 4;

    g_profiler_update_thread = std::move(xe::threading::Thread::Create(
        slimparams, GuestProfilerUpdateThreadProc));
  }
#endif

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

uint64_t ReadCapstoneReg(HostThreadContext* context, x86_reg reg) {
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
void X64Backend::RecordMMIOExceptionForGuestInstruction(void* host_address) {
  uint64_t host_addr_u64 = (uint64_t)host_address;

  auto fnfor = code_cache()->LookupFunction(host_addr_u64);
  if (fnfor) {
    uint32_t guestaddr = fnfor->MapMachineCodeToGuestAddress(host_addr_u64);

    Module* guest_module = fnfor->module();
    if (guest_module) {
      XexModule* xex_guest_module = dynamic_cast<XexModule*>(guest_module);

      if (xex_guest_module) {
        cpu::InfoCacheFlags* icf =
            xex_guest_module->GetInstructionAddressFlags(guestaddr);

        if (icf) {
          icf->accessed_mmio = true;
        }
      }
    }
  }
}
bool X64Backend::ExceptionCallback(Exception* ex) {
  if (ex->code() != Exception::Code::kIllegalInstruction) {
    // We only care about illegal instructions. Other things will be handled by
    // other handlers (probably). If nothing else picks it up we'll be called
    // with OnUnhandledException to do real crash handling.
    return false;
  }

  // processor_->memory()->LookupVirtualMappedRange()

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

X64HelperEmitter::X64HelperEmitter(X64Backend* backend,
                                   XbyakAllocator* allocator)
    : X64Emitter(backend, allocator) {}

X64HelperEmitter::~X64HelperEmitter() {}
void* X64HelperEmitter::EmitCurrentForOffsets(const _code_offsets& code_offsets,
                                              size_t stack_size) {
  EmitFunctionInfo func_info = {};
  func_info.code_size.total = getSize();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = getSize() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;
  func_info.stack_size = stack_size;

  void* fn = Emplace(func_info);
  return fn;
}
HostToGuestThunk X64HelperEmitter::EmitHostToGuestThunk() {
  // rcx = target
  // rdx = arg0 (context)
  // r8 = arg1 (guest return address)

  _code_offsets code_offsets = {};

  const size_t stack_size = StackLayout::THUNK_STACK_SIZE;

  code_offsets.prolog = getSize();

  // rsp + 0 = return address
  mov(qword[rsp + 8 * 3], r8);
  mov(qword[rsp + 8 * 2], rdx);
  mov(qword[rsp + 8 * 1], rcx);
  sub(rsp, stack_size);

  code_offsets.prolog_stack_alloc = getSize();
  code_offsets.body = getSize();

  // Save nonvolatile registers.
  EmitSaveNonvolatileRegs();

  mov(rax, rcx);
  mov(rsi, rdx);                                                    // context
  mov(rdi, ptr[rdx + offsetof(ppc::PPCContext, virtual_membase)]);  // membase
  mov(rcx, r8);  // return address
  call(rax);
  vzeroupper();
  EmitLoadNonvolatileRegs();

  code_offsets.epilog = getSize();

  add(rsp, stack_size);
  mov(rcx, qword[rsp + 8 * 1]);
  mov(rdx, qword[rsp + 8 * 2]);
  mov(r8, qword[rsp + 8 * 3]);
  ret();

  code_offsets.tail = getSize();

  assert_zero(code_offsets.prolog);
  EmitFunctionInfo func_info = {};
  func_info.code_size.total = getSize();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = getSize() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;
  func_info.stack_size = stack_size;

  void* fn = Emplace(func_info);
  return (HostToGuestThunk)fn;
}

GuestToHostThunk X64HelperEmitter::EmitGuestToHostThunk() {
  // rcx = target function
  // rdx = arg0
  // r8  = arg1
  // r9  = arg2

  _code_offsets code_offsets = {};

  const size_t stack_size = StackLayout::THUNK_STACK_SIZE;

  code_offsets.prolog = getSize();

  // rsp + 0 = return address
  sub(rsp, stack_size);

  code_offsets.prolog_stack_alloc = getSize();
  code_offsets.body = getSize();
  // chrispy: added this for proper vmsum impl, avx2 bitshifts
  vzeroupper();
  // Save off volatile registers.
  EmitSaveVolatileRegs();

  mov(rax, rcx);              // function
  mov(rcx, GetContextReg());  // context
  call(rax);

  EmitLoadVolatileRegs();

  code_offsets.epilog = getSize();

  add(rsp, stack_size);
  ret();

  code_offsets.tail = getSize();

  assert_zero(code_offsets.prolog);
  EmitFunctionInfo func_info = {};
  func_info.code_size.total = getSize();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = getSize() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;
  func_info.stack_size = stack_size;

  void* fn = Emplace(func_info);
  return (GuestToHostThunk)fn;
}

// X64Emitter handles actually resolving functions.
uint64_t ResolveFunction(void* raw_context, uint64_t target_address);

ResolveFunctionThunk X64HelperEmitter::EmitResolveFunctionThunk() {
  // ebx = target PPC address
  // rcx = context

  _code_offsets code_offsets = {};

  const size_t stack_size = StackLayout::THUNK_STACK_SIZE;

  code_offsets.prolog = getSize();

  // rsp + 0 = return address
  sub(rsp, stack_size);

  code_offsets.prolog_stack_alloc = getSize();
  code_offsets.body = getSize();

  // Save volatile registers
  EmitSaveVolatileRegs();

  mov(rcx, rsi);  // context
  mov(rdx, rbx);
  mov(rax, reinterpret_cast<uint64_t>(&ResolveFunction));
  call(rax);

  EmitLoadVolatileRegs();

  code_offsets.epilog = getSize();

  add(rsp, stack_size);
  jmp(rax);

  code_offsets.tail = getSize();

  assert_zero(code_offsets.prolog);
  EmitFunctionInfo func_info = {};
  func_info.code_size.total = getSize();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = getSize() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;
  func_info.stack_size = stack_size;

  void* fn = Emplace(func_info);
  return (ResolveFunctionThunk)fn;
}
// r11 = size of callers stack, r8 = return address w/ adjustment
// i'm not proud of this code, but it shouldn't be executed frequently at all
void* X64HelperEmitter::EmitGuestAndHostSynchronizeStackHelper() {
  _code_offsets code_offsets = {};
  code_offsets.prolog = getSize();
  push(rbx);
  mov(rbx, GetBackendCtxPtr(offsetof(X64BackendContext, stackpoints)));
  mov(eax,
      GetBackendCtxPtr(offsetof(X64BackendContext, current_stackpoint_depth)));

  lea(ecx, ptr[eax - 1]);
  mov(r9d, ptr[GetContextReg() + offsetof(ppc::PPCContext, r[1])]);

  Xbyak::Label looper{};
  Xbyak::Label loopout{};
  Xbyak::Label signed_underflow{};
  xor_(r12d, r12d);

  // todo: should use Loop instruction here if hasFastLoop,
  // currently xbyak does not support it but its super easy to modify xbyak to
  //  have it
  L(looper);
  imul(edx, ecx, sizeof(X64BackendStackpoint));
  mov(r10d, ptr[rbx + rdx + offsetof(X64BackendStackpoint, guest_stack_)]);

  cmp(r10d, r9d);

  jge(loopout, T_NEAR);

  inc(r12d);

  if (IsFeatureEnabled(kX64FlagsIndependentVars)) {
    dec(ecx);
  } else {
    sub(ecx, 1);
  }
  js(signed_underflow, T_NEAR);  // should be impossible!!

  jmp(looper, T_NEAR);
  L(loopout);
  Xbyak::Label skip_adjust{};
  cmp(r12d, 1);  // should never happen?
  jle(skip_adjust, T_NEAR);
  Xbyak::Label we_good{};

  // now we need to make sure that the return address matches

  // mov(r9d, ptr[GetContextReg() + offsetof(ppc::PPCContext, lr)]);
  pop(r9);  // guest retaddr
  // r10d = the guest_stack
  // while guest_stack is equal and return address is not equal, decrement

  Xbyak::Label search_for_retaddr{};
  Xbyak::Label we_good_but_increment{};
  L(search_for_retaddr);

  imul(edx, ecx, sizeof(X64BackendStackpoint));

  cmp(r10d, ptr[rbx + rdx + offsetof(X64BackendStackpoint, guest_stack_)]);

  jnz(we_good_but_increment, T_NEAR);

  cmp(r9d,
      ptr[rbx + rdx + offsetof(X64BackendStackpoint, guest_return_address_)]);
  jz(we_good, T_NEAR);  // stack is equal, return address is equal, we've got
                        // our destination stack
  dec(ecx);
  jmp(search_for_retaddr, T_NEAR);
  Xbyak::Label checkbp{};

  L(we_good_but_increment);
  add(edx, sizeof(X64BackendStackpoint));
  inc(ecx);
  jmp(checkbp, T_NEAR);
  L(we_good);
  //we're popping this return address, so go down by one
  sub(edx, sizeof(X64BackendStackpoint));
  dec(ecx);
  L(checkbp);
  mov(rsp, ptr[rbx + rdx + offsetof(X64BackendStackpoint, host_stack_)]);
  if (IsFeatureEnabled(kX64FlagsIndependentVars)) {
    inc(ecx);
  } else {
    add(ecx, 1);
  }

  sub(rsp, r11);  // adjust stack

  mov(GetBackendCtxPtr(offsetof(X64BackendContext, current_stackpoint_depth)),
      ecx);  // set next stackpoint index to be after the one we restored to
  jmp(r8);
  L(skip_adjust);
  pop(rbx);
  jmp(r8);  // return to caller
  code_offsets.prolog_stack_alloc = getSize();
  code_offsets.body = getSize();
  code_offsets.epilog = getSize();
  code_offsets.tail = getSize();

  L(signed_underflow);
  // find a good, compact way to signal error here
  //  maybe an invalid opcode that we execute, then detect in an exception
  // handler?

  this->DebugBreak();
  return EmitCurrentForOffsets(code_offsets);
}

void* X64HelperEmitter::EmitGuestAndHostSynchronizeStackSizeLoadThunk(
    void* sync_func, unsigned stack_element_size) {
  _code_offsets code_offsets = {};
  code_offsets.prolog = getSize();
  pop(r8);  // return address

  switch (stack_element_size) {
    case 4:
      mov(r11d, ptr[r8]);
      break;
    case 2:
      movzx(r11d, word[r8]);
      break;
    case 1:
      movzx(r11d, byte[r8]);
      break;
  }
  add(r8, stack_element_size);
  jmp(sync_func, T_NEAR);
  code_offsets.prolog_stack_alloc = getSize();
  code_offsets.body = getSize();
  code_offsets.epilog = getSize();
  code_offsets.tail = getSize();
  return EmitCurrentForOffsets(code_offsets);
}
void X64HelperEmitter::EmitSaveVolatileRegs() {
  // Save off volatile registers.
  // mov(qword[rsp + offsetof(StackLayout::Thunk, r[0])], rax);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[1])], rcx);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[2])], rdx);
#if XE_PLATFORM_LINUX
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[3])], rsi);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[4])], rdi);
#endif
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[5])], r8);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[6])], r9);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[7])], r10);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[8])], r11);

  // vmovaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[0])], xmm0);
  vmovaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[1])], xmm1);
  vmovaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[2])], xmm2);
  vmovaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[3])], xmm3);
  vmovaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[4])], xmm4);
  vmovaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[5])], xmm5);
}

void X64HelperEmitter::EmitLoadVolatileRegs() {
  // mov(rax, qword[rsp + offsetof(StackLayout::Thunk, r[0])]);
  mov(rcx, qword[rsp + offsetof(StackLayout::Thunk, r[1])]);
  mov(rdx, qword[rsp + offsetof(StackLayout::Thunk, r[2])]);
#if XE_PLATFORM_LINUX
  mov(rsi, qword[rsp + offsetof(StackLayout::Thunk, r[3])]);
  mov(rdi, qword[rsp + offsetof(StackLayout::Thunk, r[4])]);
#endif
  mov(r8, qword[rsp + offsetof(StackLayout::Thunk, r[5])]);
  mov(r9, qword[rsp + offsetof(StackLayout::Thunk, r[6])]);
  mov(r10, qword[rsp + offsetof(StackLayout::Thunk, r[7])]);
  mov(r11, qword[rsp + offsetof(StackLayout::Thunk, r[8])]);

  // vmovaps(xmm0, qword[rsp + offsetof(StackLayout::Thunk, xmm[0])]);
  vmovaps(xmm1, qword[rsp + offsetof(StackLayout::Thunk, xmm[1])]);
  vmovaps(xmm2, qword[rsp + offsetof(StackLayout::Thunk, xmm[2])]);
  vmovaps(xmm3, qword[rsp + offsetof(StackLayout::Thunk, xmm[3])]);
  vmovaps(xmm4, qword[rsp + offsetof(StackLayout::Thunk, xmm[4])]);
  vmovaps(xmm5, qword[rsp + offsetof(StackLayout::Thunk, xmm[5])]);
}

void X64HelperEmitter::EmitSaveNonvolatileRegs() {
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[0])], rbx);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[1])], rbp);
#if XE_PLATFORM_WIN32
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[2])], rcx);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[3])], rsi);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[4])], rdi);
#endif
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[5])], r12);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[6])], r13);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[7])], r14);
  mov(qword[rsp + offsetof(StackLayout::Thunk, r[8])], r15);

  // SysV does not have nonvolatile XMM registers.
#if XE_PLATFORM_WIN32
  vmovaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[0])], xmm6);
  vmovaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[1])], xmm7);
  vmovaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[2])], xmm8);
  vmovaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[3])], xmm9);
  vmovaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[4])], xmm10);
  vmovaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[5])], xmm11);
  vmovaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[6])], xmm12);
  vmovaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[7])], xmm13);
  vmovaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[8])], xmm14);
  vmovaps(qword[rsp + offsetof(StackLayout::Thunk, xmm[9])], xmm15);
#endif
}

void X64HelperEmitter::EmitLoadNonvolatileRegs() {
  mov(rbx, qword[rsp + offsetof(StackLayout::Thunk, r[0])]);
  mov(rbp, qword[rsp + offsetof(StackLayout::Thunk, r[1])]);
#if XE_PLATFORM_WIN32
  mov(rcx, qword[rsp + offsetof(StackLayout::Thunk, r[2])]);
  mov(rsi, qword[rsp + offsetof(StackLayout::Thunk, r[3])]);
  mov(rdi, qword[rsp + offsetof(StackLayout::Thunk, r[4])]);
#endif
  mov(r12, qword[rsp + offsetof(StackLayout::Thunk, r[5])]);
  mov(r13, qword[rsp + offsetof(StackLayout::Thunk, r[6])]);
  mov(r14, qword[rsp + offsetof(StackLayout::Thunk, r[7])]);
  mov(r15, qword[rsp + offsetof(StackLayout::Thunk, r[8])]);

#if XE_PLATFORM_WIN32
  vmovaps(xmm6, qword[rsp + offsetof(StackLayout::Thunk, xmm[0])]);
  vmovaps(xmm7, qword[rsp + offsetof(StackLayout::Thunk, xmm[1])]);
  vmovaps(xmm8, qword[rsp + offsetof(StackLayout::Thunk, xmm[2])]);
  vmovaps(xmm9, qword[rsp + offsetof(StackLayout::Thunk, xmm[3])]);
  vmovaps(xmm10, qword[rsp + offsetof(StackLayout::Thunk, xmm[4])]);
  vmovaps(xmm11, qword[rsp + offsetof(StackLayout::Thunk, xmm[5])]);
  vmovaps(xmm12, qword[rsp + offsetof(StackLayout::Thunk, xmm[6])]);
  vmovaps(xmm13, qword[rsp + offsetof(StackLayout::Thunk, xmm[7])]);
  vmovaps(xmm14, qword[rsp + offsetof(StackLayout::Thunk, xmm[8])]);
  vmovaps(xmm15, qword[rsp + offsetof(StackLayout::Thunk, xmm[9])]);
#endif
}
void X64Backend::InitializeBackendContext(void* ctx) {
  X64BackendContext* bctx = BackendContextForGuestContext(ctx);
  bctx->mxcsr_fpu =
      DEFAULT_FPU_MXCSR;  // idk if this is right, check on rgh what the
                          // rounding on ppc is at startup

  /*
          todo: stackpoint arrays should be pooled virtual memory at the very
     least there may be some fancy virtual address tricks we can do here

  */

  bctx->stackpoints = cvars::enable_host_guest_stack_synchronization
                          ? new X64BackendStackpoint[cvars::max_stackpoints]
                          : nullptr;
  bctx->current_stackpoint_depth = 0;
  bctx->mxcsr_vmx = DEFAULT_VMX_MXCSR;
  bctx->flags = 0;
  // https://media.discordapp.net/attachments/440280035056943104/1000765256643125308/unknown.png
  bctx->Ox1000 = 0x1000;
  bctx->guest_tick_count = Clock::GetGuestTickCountPointer();
}
void X64Backend::DeinitializeBackendContext(void* ctx) {
  X64BackendContext* bctx = BackendContextForGuestContext(ctx);

  if (bctx->stackpoints) {
    delete[] bctx->stackpoints;
    bctx->stackpoints = nullptr;
  }
}

void X64Backend::PrepareForReentry(void* ctx) {
  X64BackendContext* bctx = BackendContextForGuestContext(ctx);

  bctx->current_stackpoint_depth = 0;
}

const uint32_t mxcsr_table[8] = {
    0x1F80, 0x7F80, 0x5F80, 0x3F80, 0x9F80, 0xFF80, 0xDF80, 0xBF80,
};

void X64Backend::SetGuestRoundingMode(void* ctx, unsigned int mode) {
  X64BackendContext* bctx = BackendContextForGuestContext(ctx);

  uint32_t control = mode & 7;
  _mm_setcsr(mxcsr_table[control]);
  bctx->mxcsr_fpu = mxcsr_table[control];
  ((ppc::PPCContext*)ctx)->fpscr.bits.rn = control;
}

#if XE_X64_PROFILER_AVAILABLE == 1
uint64_t* X64Backend::GetProfilerRecordForFunction(uint32_t guest_address) {
  // who knows, we might want to compile different versions of a function one
  // day
  auto entry = profiler_data_.find(guest_address);

  if (entry != profiler_data_.end()) {
    return &entry->second;
  } else {
    profiler_data_[guest_address] = 0;

    return &profiler_data_[guest_address];
  }
}

#endif
}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
