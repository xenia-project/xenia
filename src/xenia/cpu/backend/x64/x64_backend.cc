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

  void* EmitTryAcquireReservationHelper();
  void* EmitReservedStoreHelper(bool bit64 = false);

  void* EmitScalarVRsqrteHelper();
  void* EmitVectorVRsqrteHelper(void* scalar_helper);

  void* EmitFrsqrteHelper();

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

#if XE_PLATFORM_WIN32
static constexpr unsigned char guest_trampoline_template[] = {
    0x48, 0xBA, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x00, 0x49,
    0xB8, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x00, 0x48, 0xB9,
    0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x00, 0x48, 0xB8, 0x99,
    0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x00, 0xFF, 0xE0};

#else
// sysv x64 abi, exact same offsets for args
static constexpr unsigned char guest_trampoline_template[] = {
    0x48, 0xBF, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x00, 0x48,
    0xBE, 0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x00, 0x48, 0xB9,
    0x99, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x00, 0x48, 0xB8, 0x99,
    0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x00, 0xFF, 0xE0};
#endif
static constexpr uint32_t guest_trampoline_template_offset_arg1 = 2,
                          guest_trampoline_template_offset_arg2 = 0xC,
                          guest_trampoline_template_offset_rcx = 0x16,
                          guest_trampoline_template_offset_rax = 0x20;
X64Backend::X64Backend() : Backend(), code_cache_(nullptr) {
  if (cs_open(CS_ARCH_X86, CS_MODE_64, &capstone_handle_) != CS_ERR_OK) {
    assert_always("Failed to initialize capstone");
  }
  cs_option(capstone_handle_, CS_OPT_SYNTAX, CS_OPT_SYNTAX_INTEL);
  cs_option(capstone_handle_, CS_OPT_DETAIL, CS_OPT_ON);
  cs_option(capstone_handle_, CS_OPT_SKIPDATA, CS_OPT_OFF);
  uint32_t base_address = 0x10000;
  void* buf_trampoline_code = nullptr;
  while (base_address < 0x80000000) {
    buf_trampoline_code = memory::AllocFixed(
        (void*)(uintptr_t)base_address,
        sizeof(guest_trampoline_template) * MAX_GUEST_TRAMPOLINES,
        xe::memory::AllocationType::kReserveCommit,
        xe::memory::PageAccess::kExecuteReadWrite);
    if (!buf_trampoline_code) {
      base_address += 65536;
    } else {
      break;
    }
  }
  xenia_assert(buf_trampoline_code);
  guest_trampoline_memory_ = (uint8_t*)buf_trampoline_code;
  guest_trampoline_address_bitmap_.Resize(MAX_GUEST_TRAMPOLINES);
}

X64Backend::~X64Backend() {
  if (capstone_handle_) {
    cs_close(&capstone_handle_);
  }

  X64Emitter::FreeConstData(emitter_data_);
  ExceptionHandler::Uninstall(&ExceptionCallbackThunk, this);
  if (guest_trampoline_memory_) {
    memory::DeallocFixed(
        guest_trampoline_memory_,
        sizeof(guest_trampoline_template) * MAX_GUEST_TRAMPOLINES,
        memory::DeallocationType::kRelease);
    guest_trampoline_memory_ = nullptr;
  }
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
  // HV range
  code_cache()->CommitExecutableRange(GUEST_TRAMPOLINE_BASE,
                                      GUEST_TRAMPOLINE_END);
  // Allocate emitter constant data.
  emitter_data_ = X64Emitter::PlaceConstData();

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
  try_acquire_reservation_helper_ =
      thunk_emitter.EmitTryAcquireReservationHelper();
  reserved_store_32_helper = thunk_emitter.EmitReservedStoreHelper(false);
  reserved_store_64_helper = thunk_emitter.EmitReservedStoreHelper(true);
  vrsqrtefp_scalar_helper = thunk_emitter.EmitScalarVRsqrteHelper();
  vrsqrtefp_vector_helper =
      thunk_emitter.EmitVectorVRsqrteHelper(vrsqrtefp_scalar_helper);
  frsqrtefp_helper = thunk_emitter.EmitFrsqrteHelper();
  // Set the code cache to use the ResolveFunction thunk for default
  // indirections.
  assert_zero(uint64_t(resolve_function_thunk_) & 0xFFFFFFFF00000000ull);
  code_cache_->set_indirection_default(
      uint32_t(uint64_t(resolve_function_thunk_)));

  // Allocate some special indirections.
  code_cache_->CommitExecutableRange(0x9FFF0000, 0x9FFFFFFF);

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
  // we're popping this return address, so go down by one
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

void* X64HelperEmitter::EmitScalarVRsqrteHelper() {
  _code_offsets code_offsets = {};

  Xbyak::Label L18, L2, L35, L4, L9, L8, L10, L11, L12, L13, L1;
  Xbyak::Label LC1, _LCPI3_1;
  Xbyak::Label handle_denormal_input;
  Xbyak::Label specialcheck_1, convert_to_signed_inf_and_ret, handle_oddball_denormal;

  auto emulate_lzcnt_helper_unary_reg = [this](auto& reg, auto& scratch_reg) {
    inLocalLabel();
    Xbyak::Label end_lzcnt;
    bsr(scratch_reg, reg);
    mov(reg, 0x20);
    jz(end_lzcnt);
    xor_(scratch_reg, 0x1F);
    mov(reg, scratch_reg);
    L(end_lzcnt);
    outLocalLabel();
  };

  vmovd(r8d, xmm0);
  vmovaps(xmm1, xmm0);
  mov(ecx, r8d);
  //extract mantissa
  and_(ecx, 0x7fffff);
  mov(edx, ecx);
  cmp(r8d, 0xff800000);
  jz(specialcheck_1, CodeGenerator::T_NEAR);
  //is exponent zero?
  test(r8d, 0x7f800000);
  jne(L18);
  test(ecx, ecx);
  jne(L2);

  L(L18);
  //extract biased exponent and unbias
  mov(r9d, r8d);
  shr(r9d, 23);
  movzx(r9d, r9b);
  lea(eax, ptr[r9 - 127]);
  cmp(r9d, 255);
  jne(L4);
  jmp(L35);

  L(L2);

  bt(GetBackendFlagsPtr(), kX64BackendNJMOn);
  jnc(handle_denormal_input, CodeGenerator::T_NEAR);

  // handle denormal input with NJM on
  // denorms get converted to zero w/ input sign, jump to our label
  // that handles inputs of 0 for this

  jmp(convert_to_signed_inf_and_ret);
  L(L35);

  vxorps(xmm0, xmm0, xmm0);
  mov(eax, 128);
  vcomiss(xmm1, xmm0);
  jb(L4);
  test(ecx, ecx);
  jne(L8);
  ret();

  L(L4);
  cmp(eax, 128);
  jne(L9);
  vxorps(xmm0, xmm0, xmm0);
  vcomiss(xmm0, xmm1);
  jbe(L9);
  vmovss(xmm2, ptr[rip+LC1]);
  vandps(xmm1, GetXmmConstPtr(XMMSignMaskF32));

  test(edx, edx);
  jne(L8);
  vorps(xmm0, xmm2, xmm2);
  ret();

  L(L9);
  test(edx, edx);
  je(L10);
  cmp(eax, 128);
  jne(L11);
  L(L8);
  or_(r8d, 0x400000);
  vmovd(xmm0, r8d);
  ret();
  L(L10);
  test(r9d, r9d);
  jne(L11);
  L(convert_to_signed_inf_and_ret);
  not_(r8d);
  shr(r8d, 31);

  lea(rdx, ptr[rip + _LCPI3_1]);
  shl(r8d, 2);
  vmovss(xmm0, ptr[r8 + rdx]);
  ret();

  L(L11);
  vxorps(xmm2, xmm2, xmm2);
  vmovss(xmm0, ptr[rip+LC1]);
  vcomiss(xmm2, xmm1);
  ja(L1, CodeGenerator::T_NEAR);
  mov(ecx, 127);
  sal(eax, 4);
  sub(ecx, r9d);
  mov(r9d, edx);
  and_(eax, 16);
  shr(edx, 9);
  shr(r9d, 19);
  and_(edx, 1023);
  sar(ecx, 1);
  or_(eax, r9d);
  xor_(eax, 16);
  mov(r9d, ptr[backend()->LookupXMMConstantAddress32(XMMVRsqrteTableStart) +
               rax * 4]);
  mov(eax, r9d);
  shr(r9d, 16);
  imul(edx, r9d);
  sal(eax, 10);
  and_(eax, 0x3fffc00);
  sub(eax, edx);
  bt(eax, 25);
  jc(L12);
  mov(edx, eax);
  add(ecx, 6);
  and_(edx, 0x1ffffff);

  if (IsFeatureEnabled(kX64EmitLZCNT)) {
    lzcnt(edx, edx);
  } else {
    emulate_lzcnt_helper_unary_reg(edx, r9d);
  }

  lea(r9d, ptr[rdx - 6]);
  sub(ecx, edx);
  if (IsFeatureEnabled(kX64EmitBMI2)) {
    shlx(eax, eax, r9d);
  } else {
    xchg(ecx, r9d);
    shl(eax, cl);
    xchg(ecx, r9d);
  }

  L(L12);
  test(al, 5);
  je(L13);
  test(al, 2);
  je(L13);
  add(eax, 4);

  L(L13);
  sal(ecx, 23);
  and_(r8d, 0x80000000);
  shr(eax, 2);
  add(ecx, 0x3f800000);
  and_(eax, 0x7fffff);
  vxorps(xmm1, xmm1);
  or_(ecx, r8d);
  or_(ecx, eax);
  vmovd(xmm0, ecx);
  vaddss(xmm0, xmm1);//apply DAZ behavior to output

  L(L1);
  ret();

  L(handle_denormal_input);
  mov(r9d, r8d);
  and_(r9d, 0x7FFFFFFF);
  cmp(r9d, 0x400000);
  jz(handle_oddball_denormal);
  if (IsFeatureEnabled(kX64EmitLZCNT)) {
    lzcnt(ecx, ecx);
  } else {
    emulate_lzcnt_helper_unary_reg(ecx, r9d);
  }

  mov(r9d, 9);
  mov(eax, -118);
  lea(edx, ptr[rcx - 8]);
  sub(r9d, ecx);
  sub(eax, ecx);
  if (IsFeatureEnabled(kX64EmitBMI2)) {
    shlx(edx, r8d, edx);
  } else {
    xchg(ecx, edx);
    // esi is just the value of xmm0's low word, so we can restore it from there
    shl(r8d, cl);
    mov(ecx, edx);  // restore ecx, dont xchg because we're going to spoil edx anyway
    mov(edx, r8d);
    vmovd(r8d, xmm0);
  }
  and_(edx, 0x7ffffe);
  jmp(L4);

  L(specialcheck_1);
  //should be extremely rare
  vmovss(xmm0, ptr[rip+LC1]);
  ret();

  L(handle_oddball_denormal);
  not_(r8d);
  lea(r9, ptr[rip + LC1]);

  shr(r8d, 31);
  movss(xmm0, ptr[r9 + r8 * 4]);
  ret();

  L(_LCPI3_1);
  dd(0xFF800000);
  dd(0x7F800000);
  L(LC1);
  //the position of 7FC00000 here matters, this address will be indexed in handle_oddball_denormal
  dd(0x7FC00000);
  dd(0x5F34FD00);

  code_offsets.prolog_stack_alloc = getSize();
  code_offsets.body = getSize();
  code_offsets.prolog = getSize();
  code_offsets.epilog = getSize();
  code_offsets.tail = getSize();
  return EmitCurrentForOffsets(code_offsets);
}

void* X64HelperEmitter::EmitVectorVRsqrteHelper(void* scalar_helper) {
  _code_offsets code_offsets = {};
  Xbyak::Label check_scalar_operation_in_vmx, actual_vector_version;
  auto result_ptr =
      GetBackendCtxPtr(offsetof(X64BackendContext, helper_scratch_xmms[0]));
  auto counter_ptr = GetBackendCtxPtr(offsetof(X64BackendContext, helper_scratch_u64s[2]));
  counter_ptr.setBit(64);

  //shuffle and xor to check whether all lanes are equal
  //sadly has to leave the float pipeline for the vptest, which is moderate yikes
  vmovhlps(xmm2, xmm0, xmm0);
  vmovsldup(xmm1, xmm0);
  vxorps(xmm1, xmm1, xmm0);
  vxorps(xmm2, xmm2, xmm0);
  vorps(xmm2, xmm1, xmm2);
  vptest(xmm2, xmm2);
  jnz(check_scalar_operation_in_vmx);
  //jmp(scalar_helper, CodeGenerator::T_NEAR);
  call(scalar_helper);
  vshufps(xmm0, xmm0, xmm0, 0);
  ret();

  L(check_scalar_operation_in_vmx);

  vptest(xmm0, ptr[backend()->LookupXMMConstantAddress(XMMThreeFloatMask)]);
  jnz(actual_vector_version);
  vshufps(xmm0, xmm0,xmm0, _MM_SHUFFLE(3, 3, 3, 3));
  call(scalar_helper);
  // this->DebugBreak();
  vinsertps(xmm0, xmm0, (3 << 4) | (0 << 6));

  vblendps(xmm0, xmm0, ptr[backend()->LookupXMMConstantAddress(XMMFloatInf)],
           0b0111);

  ret();

  L(actual_vector_version);

  xor_(ecx, ecx);
  vmovaps(result_ptr, xmm0);

  mov(counter_ptr, rcx);
  Xbyak::Label loop;

  L(loop);
  lea(rax, result_ptr);
  vmovss(xmm0, ptr[rax+rcx*4]);
  call(scalar_helper);
  mov(rcx, counter_ptr);
  lea(rax, result_ptr);
  vmovss(ptr[rax+rcx*4], xmm0);
  inc(ecx);
  cmp(ecx, 4);
  mov(counter_ptr, rcx);
  jl(loop);
  vmovaps(xmm0, result_ptr);
  ret();
  code_offsets.prolog_stack_alloc = getSize();
  code_offsets.body = getSize();
  code_offsets.epilog = getSize();
  code_offsets.tail = getSize();
  code_offsets.prolog = getSize();
  return EmitCurrentForOffsets(code_offsets);
}

void* X64HelperEmitter::EmitFrsqrteHelper() {
  _code_offsets code_offsets = {};
  code_offsets.prolog_stack_alloc = getSize();
  code_offsets.body = getSize();
  code_offsets.epilog = getSize();
  code_offsets.tail = getSize();
  code_offsets.prolog = getSize();

  Xbyak::Label L2, L7, L6, L9, L1, L12, L24, L3, L25, frsqrte_table2, LC1;
  bt(GetBackendFlagsPtr(), kX64BackendNonIEEEMode);
  vmovq(rax, xmm0);
  jc(L24, CodeGenerator::T_NEAR);
  L(L2);
  mov(rcx, rax);
  add(rcx, rcx);
  je(L3, CodeGenerator::T_NEAR);
  mov(rdx, 0x7ff0000000000000ULL);
  vxorpd(xmm1, xmm1, xmm1);
  if (IsFeatureEnabled(kX64EmitBMI1)) {
    andn(rcx, rax, rdx);
  } else {
    mov(rcx, rax);
    not_(rcx);
    and_(rcx, rdx);
  }

  jne(L6);
  cmp(rax, rdx);
  je(L1, CodeGenerator::T_NEAR);
  mov(r8, rax);
  sal(r8, 12);
  jne(L7);
  vcomisd(xmm0, xmm1);
  jb(L12, CodeGenerator::T_NEAR);

  L(L7);
  mov(rdx, 0x7ff8000000000000ULL);
  or_(rax, rdx);
  vmovq(xmm1, rax);
  vmovapd(xmm0, xmm1);
  ret();

  L(L6);
  vcomisd(xmm1, xmm0);
  ja(L12, CodeGenerator::T_NEAR);
  mov(rcx, rax);
  mov(rdx, 0xfffffffffffffULL);
  shr(rcx, 52);
  and_(ecx, 2047);
  and_(rax, rdx);
  je(L9);
  test(ecx, ecx);
  je(L25, CodeGenerator::T_NEAR);

  L(L9);
  lea(edx, ptr[0 + rcx * 8]);
  shr(rax, 49);
  sub(ecx, 1023);
  and_(edx, 8);
  and_(eax, 7);
  shr(ecx, 1);
  or_(eax, edx);
  mov(edx, 1022);
  xor_(eax, 8);
  sub(edx, ecx);
  lea(rcx, ptr[rip + frsqrte_table2]);
  movzx(eax, byte[rax+rcx]);
  sal(rdx, 52);
  sal(rax, 44);
  or_(rax, rdx);
  vmovq(xmm1, rax);

  L(L1);
  vmovapd(xmm0, xmm1);
  ret();

  L(L12);
  vmovsd(xmm1, qword[rip + LC1]);
  vmovapd(xmm0, xmm1);
  ret();

  L(L24);
  mov(r8, rax);
  sal(r8, 12);
  je(L2);
  mov(rdx, 0x7ff0000000000000);
  test(rax, rdx);
  jne(L2);
  mov(rdx, 0x8000000000000000ULL);
  and_(rax, rdx);

  L(L3);
  mov(rdx, 0x8000000000000000ULL);
  and_(rax, rdx);
  mov(rdx, 0x7ff0000000000000ULL);
  or_(rax, rdx);
  vmovq(xmm1, rax);
  vmovapd(xmm0, xmm1);
  ret();

  L(L25);
  if (IsFeatureEnabled(kX64EmitLZCNT)) {
    lzcnt(rdx, rax);
  } else {
    Xbyak::Label end_lzcnt;
    bsr(rcx, rax);
    mov(rdx, 0x40);
    jz(end_lzcnt);
    xor_(rcx, 0x3F);
    mov(rdx, rcx);
    L(end_lzcnt);
  }
  lea(ecx, ptr[rdx - 11]);
  if (IsFeatureEnabled(kX64EmitBMI2)) {
    shlx(rax, rax, rcx);
  } else {
    shl(rax, cl);
  }
  mov(ecx, 12);
  sub(ecx, edx);
  jmp(L9, CodeGenerator::T_NEAR);

  L(frsqrte_table2);
  static constexpr unsigned char table_values[] = {
      241u, 216u, 192u, 168u, 152u, 136u, 128u, 112u,
      96u,  76u,  60u,  48u,  32u,  24u,  16u,  8u};
  db(table_values, sizeof(table_values));

  L(LC1);
  dd(0);
  dd(0x7ff80000);
  return EmitCurrentForOffsets(code_offsets);
}

void* X64HelperEmitter::EmitTryAcquireReservationHelper() {
  _code_offsets code_offsets = {};
  code_offsets.prolog = getSize();

  Xbyak::Label already_has_a_reservation;
  Xbyak::Label acquire_new_reservation;

  btr(GetBackendFlagsPtr(), kX64BackendHasReserveBit);
  mov(r8, GetBackendCtxPtr(offsetof(X64BackendContext, reserve_helper_)));
  jc(already_has_a_reservation);

  shr(ecx, RESERVE_BLOCK_SHIFT);
  xor_(r9d, r9d);
  mov(edx, ecx);
  shr(edx, 6);  // divide by 64
  lea(rdx, ptr[r8 + rdx * 8]);
  and_(ecx, 64 - 1);

  lock();
  bts(qword[rdx], rcx);
  // set flag on local backend context for thread to indicate our previous
  // attempt to get the reservation succeeded
  setnc(r9b);  // success = bitmap did not have a set bit at the idx
  shl(r9b, kX64BackendHasReserveBit);

  mov(GetBackendCtxPtr(offsetof(X64BackendContext, cached_reserve_offset)),
      rdx);
  mov(GetBackendCtxPtr(offsetof(X64BackendContext, cached_reserve_bit)), ecx);

  or_(GetBackendCtxPtr(offsetof(X64BackendContext, flags)), r9d);
  ret();
  L(already_has_a_reservation);
  DebugBreak();

  code_offsets.prolog_stack_alloc = getSize();
  code_offsets.body = getSize();
  code_offsets.epilog = getSize();
  code_offsets.tail = getSize();
  return EmitCurrentForOffsets(code_offsets);
}
// ecx=guest addr
// r9 = host addr
// r8 = value
// if ZF is set and CF is set, we succeeded
void* X64HelperEmitter::EmitReservedStoreHelper(bool bit64) {
  _code_offsets code_offsets = {};
  code_offsets.prolog = getSize();
  Xbyak::Label done;
  Xbyak::Label reservation_isnt_for_our_addr;
  Xbyak::Label somehow_double_cleared;
  // carry must be set + zero flag must be set

  btr(GetBackendFlagsPtr(), kX64BackendHasReserveBit);

  jnc(done);

  mov(rax, GetBackendCtxPtr(offsetof(X64BackendContext, reserve_helper_)));

  shr(ecx, RESERVE_BLOCK_SHIFT);
  mov(edx, ecx);
  shr(edx, 6);  // divide by 64
  lea(rdx, ptr[rax + rdx * 8]);
  // begin acquiring exclusive access to cacheline containing our bit
  prefetchw(ptr[rdx]);

  cmp(GetBackendCtxPtr(offsetof(X64BackendContext, cached_reserve_offset)),
      rdx);
  jnz(reservation_isnt_for_our_addr);

  mov(rax,
      GetBackendCtxPtr(offsetof(X64BackendContext, cached_reserve_value_)));

  // we need modulo bitsize, it turns out bittests' modulus behavior for the
  // bitoffset only applies for register operands, for memory ones we bug out
  // todo: actually, the above note may not be true, double check it
  and_(ecx, 64 - 1);
  cmp(GetBackendCtxPtr(offsetof(X64BackendContext, cached_reserve_bit)), ecx);
  jnz(reservation_isnt_for_our_addr);

  // was our memory modified by kernel code or something?
  lock();
  if (bit64) {
    cmpxchg(ptr[r9], r8);

  } else {
    cmpxchg(ptr[r9], r8d);
  }
  // the ZF flag is unaffected by BTR! we exploit this for the retval

  // cancel our lock on the 65k block
  lock();
  btr(qword[rdx], rcx);

  jnc(somehow_double_cleared);

  L(done);
  // i don't care that theres a dependency on the prev value of rax atm
  // sadly theres no CF&ZF condition code
  setz(al);
  setc(ah);
  cmp(ax, 0x0101);
  ret();

  // could be the same label, but otherwise we don't know where we came from
  // when one gets triggered
  L(reservation_isnt_for_our_addr);
  DebugBreak();

  L(somehow_double_cleared);  // somehow, something else cleared our reserve??
  DebugBreak();

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
  bctx->flags = (1U << kX64BackendNJMOn);  // NJM on by default
  // https://media.discordapp.net/attachments/440280035056943104/1000765256643125308/unknown.png
  bctx->Ox1000 = 0x1000;
  bctx->guest_tick_count = Clock::GetGuestTickCountPointer();
  bctx->reserve_helper_ = &reserve_helper_;
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
  auto ppc_context = ((ppc::PPCContext*)ctx);
  ppc_context->fpscr.bits.rn = control;
  ppc_context->fpscr.bits.ni = control >> 2;
}

bool X64Backend::PopulatePseudoStacktrace(GuestPseudoStackTrace* st) {
  if (!cvars::enable_host_guest_stack_synchronization) {
    return false;
  }

  ThreadState* thrd_state = ThreadState::Get();
  if (!thrd_state) {
    return false;  // we're not a guest!
  }
  ppc::PPCContext* ctx = thrd_state->context();

  X64BackendContext* backend_ctx = BackendContextForGuestContext(ctx);

  uint32_t depth = backend_ctx->current_stackpoint_depth - 1;
  if (static_cast<int32_t>(depth) < 1) {
    return false;
  }
  uint32_t num_entries_to_populate =
      std::min(MAX_GUEST_PSEUDO_STACKTRACE_ENTRIES, depth);

  st->count = num_entries_to_populate;
  st->truncated_flag = num_entries_to_populate < depth ? 1 : 0;

  X64BackendStackpoint* current_stackpoint =
      &backend_ctx->stackpoints[backend_ctx->current_stackpoint_depth - 1];

  for (uint32_t stp_index = 0; stp_index < num_entries_to_populate;
       ++stp_index) {
    st->return_addrs[stp_index] = current_stackpoint->guest_return_address_;
    current_stackpoint--;
  }
  return true;
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

// todo:flush cache
uint32_t X64Backend::CreateGuestTrampoline(GuestTrampolineProc proc,
                                           void* userdata1, void* userdata2,
                                           bool longterm) {
  size_t new_index;
  if (longterm) {
    new_index = guest_trampoline_address_bitmap_.AcquireFromBack();
  } else {
    new_index = guest_trampoline_address_bitmap_.Acquire();
  }

  xenia_assert(new_index != (size_t)-1);

  uint8_t* write_pos =
      &guest_trampoline_memory_[sizeof(guest_trampoline_template) * new_index];

  memcpy(write_pos, guest_trampoline_template,
         sizeof(guest_trampoline_template));

  *reinterpret_cast<void**>(&write_pos[guest_trampoline_template_offset_arg1]) =
      userdata1;
  *reinterpret_cast<void**>(&write_pos[guest_trampoline_template_offset_arg2]) =
      userdata2;
  *reinterpret_cast<GuestTrampolineProc*>(
      &write_pos[guest_trampoline_template_offset_rcx]) = proc;
  *reinterpret_cast<GuestToHostThunk*>(
      &write_pos[guest_trampoline_template_offset_rax]) = guest_to_host_thunk_;

  uint32_t indirection_guest_addr =
      GUEST_TRAMPOLINE_BASE +
      (static_cast<uint32_t>(new_index) * GUEST_TRAMPOLINE_MIN_LEN);

  code_cache()->AddIndirection(
      indirection_guest_addr,
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(write_pos)));

  return indirection_guest_addr;
}

void X64Backend::FreeGuestTrampoline(uint32_t trampoline_addr) {
  xenia_assert(trampoline_addr >= GUEST_TRAMPOLINE_BASE &&
               trampoline_addr < GUEST_TRAMPOLINE_END);
  size_t index =
      (trampoline_addr - GUEST_TRAMPOLINE_BASE) / GUEST_TRAMPOLINE_MIN_LEN;
  guest_trampoline_address_bitmap_.Release(index);
}
}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
