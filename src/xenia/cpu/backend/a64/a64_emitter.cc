/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_emitter.h"

#include <cstddef>

#include <climits>
#include <cstring>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/assert.h"
#include "xenia/base/atomic.h"
#include "xenia/base/debugging.h"
#include "xenia/base/literals.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/base/vec128.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/backend/a64/a64_code_cache.h"
#include "xenia/cpu/backend/a64/a64_function.h"
#include "xenia/cpu/backend/a64/a64_sequences.h"
#include "xenia/cpu/backend/a64/a64_stack_layout.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/function_debug_info.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/symbol.h"
#include "xenia/cpu/thread_state.h"

#include "oaknut/feature_detection/cpu_feature.hpp"
#include "oaknut/feature_detection/feature_detection.hpp"

DEFINE_bool(debugprint_trap_log, false,
            "Log debugprint traps to the active debugger", "CPU");
DEFINE_bool(ignore_undefined_externs, true,
            "Don't exit when an undefined extern is called.", "CPU");
DEFINE_bool(emit_source_annotations, false,
            "Add extra movs and nops to make disassembly easier to read.",
            "CPU");

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

using xe::cpu::hir::HIRBuilder;
using xe::cpu::hir::Instr;
using namespace xe::literals;
using namespace oaknut::util;

static const size_t kMaxCodeSize = 1_MiB;

static const size_t kStashOffset = 32;
// static const size_t kStashOffsetHigh = 32 + 32;

// Register indices that the HIR is allowed to use for operands
const uint8_t A64Emitter::gpr_reg_map_[A64Emitter::GPR_COUNT] = {
    19, 20, 21, 22, 23, 24, 25, 26,
};

const uint8_t A64Emitter::fpr_reg_map_[A64Emitter::FPR_COUNT] = {
    8, 9, 10, 11, 12, 13, 14, 15,
};

A64Emitter::A64Emitter(A64Backend* backend)
    : CodeBlock(4_KiB),
      CodeGenerator(CodeBlock::ptr()),
      processor_(backend->processor()),
      backend_(backend),
      code_cache_(backend->code_cache()) {
  const oaknut::CpuFeatures cpu_ = oaknut::detect_features();
#define TEST_EMIT_FEATURE(emit, ext)                \
  if ((cvars::a64_extension_mask & emit) == emit) { \
    feature_flags_ |= (cpu_.has(ext) ? emit : 0);   \
  }

  // TEST_EMIT_FEATURE(kA64EmitAVX2, oaknut::util::Cpu::tAVX2);
  // TEST_EMIT_FEATURE(kA64EmitFMA, oaknut::util::Cpu::tFMA);
  // TEST_EMIT_FEATURE(kA64EmitLZCNT, oaknut::util::Cpu::tLZCNT);
  // TEST_EMIT_FEATURE(kA64EmitBMI1, oaknut::util::Cpu::tBMI1);
  // TEST_EMIT_FEATURE(kA64EmitBMI2, oaknut::util::Cpu::tBMI2);
  // TEST_EMIT_FEATURE(kA64EmitF16C, oaknut::util::Cpu::tF16C);
  // TEST_EMIT_FEATURE(kA64EmitMovbe, oaknut::util::Cpu::tMOVBE);
  // TEST_EMIT_FEATURE(kA64EmitGFNI, oaknut::util::Cpu::tGFNI);
  // TEST_EMIT_FEATURE(kA64EmitAVX512F, oaknut::util::Cpu::tAVX512F);
  // TEST_EMIT_FEATURE(kA64EmitAVX512VL, oaknut::util::Cpu::tAVX512VL);
  // TEST_EMIT_FEATURE(kA64EmitAVX512BW, oaknut::util::Cpu::tAVX512BW);
  // TEST_EMIT_FEATURE(kA64EmitAVX512DQ, oaknut::util::Cpu::tAVX512DQ);
  // TEST_EMIT_FEATURE(kA64EmitAVX512VBMI, oaknut::util::Cpu::tAVX512_VBMI);

#undef TEST_EMIT_FEATURE
}

A64Emitter::~A64Emitter() = default;

bool A64Emitter::Emit(GuestFunction* function, HIRBuilder* builder,
                      uint32_t debug_info_flags, FunctionDebugInfo* debug_info,
                      void** out_code_address, size_t* out_code_size,
                      std::vector<SourceMapEntry>* out_source_map) {
  SCOPE_profile_cpu_f("cpu");

  // Reset.
  debug_info_ = debug_info;
  debug_info_flags_ = debug_info_flags;
  trace_data_ = &function->trace_data();
  source_map_arena_.Reset();

  // Fill the generator with code.
  EmitFunctionInfo func_info = {};
  if (!Emit(builder, func_info)) {
    return false;
  }

  // Copy the final code to the cache and relocate it.
  *out_code_size = offset();
  *out_code_address = Emplace(func_info, function);

  // Stash source map.
  source_map_arena_.CloneContents(out_source_map);

  return true;
}

void* A64Emitter::Emplace(const EmitFunctionInfo& func_info,
                          GuestFunction* function) {
  // To avoid changing xbyak, we do a switcharoo here.
  // top_ points to the Xbyak buffer, and since we are in AutoGrow mode
  // it has pending relocations. We copy the top_ to our buffer, swap the
  // pointer, relocate, then return the original scratch pointer for use.
  // top_ is used by Xbyak's ready() as both write base pointer and the absolute
  // address base, which would not work on platforms not supporting writable
  // executable memory, but Xenia doesn't use absolute label addresses in the
  // generated code.

  // uint8_t* old_address = top_;
  uint32_t* old_address = CodeBlock::ptr();
  void* new_execute_address;
  void* new_write_address;

  // assert_true(func_info.code_size.total == size_);
  assert_true(func_info.code_size.total == offset());

  if (function) {
    code_cache_->PlaceGuestCode(function->address(), CodeBlock::ptr(),
                                func_info, function, new_execute_address,
                                new_write_address);
  } else {
    code_cache_->PlaceHostCode(0, CodeBlock::ptr(), func_info,
                               new_execute_address, new_write_address);
  }
  // top_ = reinterpret_cast<uint8_t*>(new_write_address);
  // set_wptr(reinterpret_cast<uint32_t*>(new_write_address));

  // ready();

  // top_ = old_address;
  set_wptr(reinterpret_cast<uint32_t*>(old_address));

  // reset();
  label_lookup_.clear();

  return new_execute_address;
}

bool A64Emitter::Emit(HIRBuilder* builder, EmitFunctionInfo& func_info) {
  oaknut::Label epilog_label;
  epilog_label_ = &epilog_label;

  // Calculate stack size. We need to align things to their natural sizes.
  // This could be much better (sort by type/etc).
  auto locals = builder->locals();
  size_t stack_offset = StackLayout::GUEST_STACK_SIZE;
  for (auto it = locals.begin(); it != locals.end(); ++it) {
    auto slot = *it;
    size_t type_size = GetTypeSize(slot->type);

    // Align to natural size.
    stack_offset = xe::align(stack_offset, type_size);
    slot->set_constant((uint32_t)stack_offset);
    stack_offset += type_size;
  }

  // Ensure 16b alignment.
  stack_offset -= StackLayout::GUEST_STACK_SIZE;
  stack_offset = xe::align(stack_offset, static_cast<size_t>(16));

  struct _code_offsets {
    size_t prolog;
    size_t prolog_stack_alloc;
    size_t body;
    size_t epilog;
    size_t tail;
  } code_offsets = {};

  code_offsets.prolog = offset();

  // Function prolog.
  // Must be 16b aligned.
  // Windows is very strict about the form of this and the epilog:
  // https://docs.microsoft.com/en-us/cpp/build/prolog-and-epilog?view=vs-2017
  // IMPORTANT: any changes to the prolog must be kept in sync with
  //     A64CodeCache, which dynamically generates exception information.
  //     Adding or changing anything here must be matched!
  const size_t stack_size = StackLayout::GUEST_STACK_SIZE + stack_offset;
  assert_true(stack_size % 16 == 0);
  func_info.stack_size = stack_size;
  stack_size_ = stack_size;

  STP(X29, X30, SP, PRE_INDEXED, -32);
  MOV(X29, SP);

  // sub(rsp, (uint32_t)stack_size);
  SUB(SP, SP, (uint32_t)stack_size);

  code_offsets.prolog_stack_alloc = offset();
  code_offsets.body = offset();

  // mov(qword[rsp + StackLayout::GUEST_CTX_HOME], GetContextReg());
  // mov(qword[rsp + StackLayout::GUEST_RET_ADDR], rcx);
  // mov(qword[rsp + StackLayout::GUEST_CALL_RET_ADDR], 0);
  STR(GetContextReg(), SP, StackLayout::GUEST_CTX_HOME);
  STR(X0, SP, StackLayout::GUEST_RET_ADDR);
  STR(XZR, SP, StackLayout::GUEST_CALL_RET_ADDR);

  // Safe now to do some tracing.
  if (debug_info_flags_ & DebugInfoFlags::kDebugInfoTraceFunctions) {
    //// We require 32-bit addresses.
    // assert_true(uint64_t(trace_data_->header()) < UINT_MAX);
    // auto trace_header = trace_data_->header();

    //// Call count.
    // lock();
    // inc(qword[low_address(&trace_header->function_call_count)]);

    //// Get call history slot.
    // static_assert(FunctionTraceData::kFunctionCallerHistoryCount == 4,
    //               "bitmask depends on count");
    // mov(rax, qword[low_address(&trace_header->function_call_count)]);
    // and_(rax, 0b00000011);

    //// Record call history value into slot (guest addr in RDX).
    // mov(dword[Xbyak::RegExp(uint32_t(uint64_t(
    //               low_address(&trace_header->function_caller_history)))) +
    //           rax * 4],
    //     edx);

    //// Calling thread. Load ax with thread ID.
    // EmitGetCurrentThreadId();
    // lock();
    // bts(qword[low_address(&trace_header->function_thread_use)], rax);
  }

  // Load membase.
  // mov(GetMembaseReg(),
  //     qword[GetContextReg() + offsetof(ppc::PPCContext, virtual_membase)]);
  LDR(GetMembaseReg(), GetContextReg(),
      offsetof(ppc::PPCContext, virtual_membase));

  // Body.
  auto block = builder->first_block();
  while (block) {
    // Mark block labels.
    auto label = block->label_head;
    while (label) {
      l(label_lookup_[label->name]);
      label = label->next;
    }

    // Process instructions.
    const Instr* instr = block->instr_head;
    while (instr) {
      const Instr* new_tail = instr;
      if (!SelectSequence(this, instr, &new_tail)) {
        // No sequence found!
        // NOTE: If you encounter this after adding a new instruction, do a full
        // rebuild!
        assert_always();
        XELOGE("Unable to process HIR opcode {}", instr->opcode->name);
        break;
      }
      instr = new_tail;
    }

    block = block->next;
  }

  // Function epilog.
  l(epilog_label);
  epilog_label_ = nullptr;
  EmitTraceUserCallReturn();
  // mov(GetContextReg(), qword[rsp + StackLayout::GUEST_CTX_HOME]);
  LDR(GetContextReg(), SP, StackLayout::GUEST_CTX_HOME);

  code_offsets.epilog = offset();

  // add(rsp, (uint32_t)stack_size);
  // ret();
  ADD(SP, SP, (uint32_t)stack_size);

  MOV(SP, X29);
  LDP(X29, X30, SP, POST_INDEXED, 32);

  RET();

  code_offsets.tail = offset();

  if (cvars::emit_source_annotations) {
    NOP();
    NOP();
    NOP();
    NOP();
    NOP();
  }

  assert_zero(code_offsets.prolog);
  func_info.code_size.total = offset();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = offset() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;

  return true;
}

void A64Emitter::MarkSourceOffset(const Instr* i) {
  auto entry = source_map_arena_.Alloc<SourceMapEntry>();
  entry->guest_address = static_cast<uint32_t>(i->src1.offset);
  entry->hir_offset = uint32_t(i->block->ordinal << 16) | i->ordinal;
  entry->code_offset = static_cast<uint32_t>(offset());

  if (cvars::emit_source_annotations) {
    NOP();
    NOP();
    // mov(eax, entry->guest_address);
    MOV(X0, entry->guest_address);
    NOP();
    NOP();
  }

  if (debug_info_flags_ & DebugInfoFlags::kDebugInfoTraceFunctionCoverage) {
    uint32_t instruction_index =
        (entry->guest_address - trace_data_->start_address()) / 4;
    // lock();
    // inc(qword[low_address(trace_data_->instruction_execute_counts() +
    //                       instruction_index * 8)]);
  }
}

void A64Emitter::EmitGetCurrentThreadId() {
  // rsi must point to context. We could fetch from the stack if needed.
  // mov(ax, word[GetContextReg() + offsetof(ppc::PPCContext, thread_id)]);
  LDRB(W0, GetContextReg(), offsetof(ppc::PPCContext, thread_id));
}

void A64Emitter::EmitTraceUserCallReturn() {}

void A64Emitter::DebugBreak() { BRK(0xF000); }

uint64_t TrapDebugPrint(void* raw_context, uint64_t address) {
  auto thread_state = *reinterpret_cast<ThreadState**>(raw_context);
  uint32_t str_ptr = uint32_t(thread_state->context()->r[3]);
  // uint16_t str_len = uint16_t(thread_state->context()->r[4]);
  auto str = thread_state->memory()->TranslateVirtual<const char*>(str_ptr);
  // TODO(benvanik): truncate to length?
  XELOGD("(DebugPrint) {}", str);

  if (cvars::debugprint_trap_log) {
    debugging::DebugPrint("(DebugPrint) {}", str);
  }

  return 0;
}

uint64_t TrapDebugBreak(void* raw_context, uint64_t address) {
  auto thread_state = *reinterpret_cast<ThreadState**>(raw_context);
  XELOGE("tw/td forced trap hit! This should be a crash!");
  if (cvars::break_on_debugbreak) {
    xe::debugging::Break();
  }
  return 0;
}

void A64Emitter::Trap(uint16_t trap_type) {
  switch (trap_type) {
    case 20:
    case 26:
      // 0x0FE00014 is a 'debug print' where r3 = buffer r4 = length
      CallNative(TrapDebugPrint, 0);
      break;
    case 0:
    case 22:
      // Always trap?
      // TODO(benvanik): post software interrupt to debugger.
      CallNative(TrapDebugBreak, 0);
      break;
    case 25:
      // ?
      break;
    default:
      XELOGW("Unknown trap type {}", trap_type);
      BRK(0xF000);
      break;
  }
}

void A64Emitter::UnimplementedInstr(const hir::Instr* i) {
  // TODO(benvanik): notify debugger.
  BRK(0xF000);
  assert_always();
}

// This is used by the A64ThunkEmitter's ResolveFunctionThunk.
uint64_t ResolveFunction(void* raw_context, uint64_t target_address) {
  auto thread_state = *reinterpret_cast<ThreadState**>(raw_context);

  // TODO(benvanik): required?
  assert_not_zero(target_address);

  auto fn = thread_state->processor()->ResolveFunction(
      static_cast<uint32_t>(target_address));
  assert_not_null(fn);
  auto a64_fn = static_cast<A64Function*>(fn);
  uint64_t addr = reinterpret_cast<uint64_t>(a64_fn->machine_code());

  return addr;
}

void A64Emitter::Call(const hir::Instr* instr, GuestFunction* function) {
  assert_not_null(function);
  auto fn = static_cast<A64Function*>(function);
  // Resolve address to the function to call and store in X16.
  if (fn->machine_code()) {
    // TODO(benvanik): is it worth it to do this? It removes the need for
    // a ResolveFunction call, but makes the table less useful.
    assert_zero(uint64_t(fn->machine_code()) & 0xFFFFFFFF00000000);
    // mov(eax, uint32_t(uint64_t(fn->machine_code())));
    MOV(X16, uint32_t(uint64_t(fn->machine_code())));
  } else if (code_cache_->has_indirection_table()) {
    // Load the pointer to the indirection table maintained in A64CodeCache.
    // The target dword will either contain the address of the generated code
    // or a thunk to ResolveAddress.
    // mov(ebx, function->address());
    // mov(eax, dword[ebx]);
    MOV(W1, function->address());
    LDR(W16, X1);
  } else {
    // Old-style resolve.
    // Not too important because indirection table is almost always available.
    // TODO: Overwrite the call-site with a straight call.
    CallNative(&ResolveFunction, function->address());
    MOV(X16, X0);
  }

  // Actually jump/call to X16.
  if (instr->flags & hir::CALL_TAIL) {
    // Since we skip the prolog we need to mark the return here.
    EmitTraceUserCallReturn();

    // Pass the callers return address over.
    // mov(rcx, qword[rsp + StackLayout::GUEST_RET_ADDR]);
    LDR(X0, SP, StackLayout::GUEST_RET_ADDR);

    // add(rsp, static_cast<uint32_t>(stack_size()));
    // jmp(rax);
    ADD(SP, SP, stack_size());
    BR(X16);
  } else {
    // Return address is from the previous SET_RETURN_ADDRESS.
    // mov(rcx, qword[rsp + StackLayout::GUEST_CALL_RET_ADDR]);
    LDR(X0, SP, StackLayout::GUEST_CALL_RET_ADDR);

    // call(rax);
    BLR(X16);
  }
}

void A64Emitter::CallIndirect(const hir::Instr* instr,
                              const oaknut::XReg& reg) {
  // Check if return.
  if (instr->flags & hir::CALL_POSSIBLE_RETURN) {
    // cmp(reg.cvt32(), dword[rsp + StackLayout::GUEST_RET_ADDR]);
    // je(epilog_label(), CodeGenerator::T_NEAR);
    LDR(W16, SP, StackLayout::GUEST_RET_ADDR);
    CMP(reg.toW(), W16);
    B(oaknut::Cond::EQ, epilog_label());
  }

  // Load the pointer to the indirection table maintained in A64CodeCache.
  // The target dword will either contain the address of the generated code
  // or a thunk to ResolveAddress.
  if (code_cache_->has_indirection_table()) {
    if (reg.toW().index() != W1.index()) {
      // mov(ebx, reg.cvt32());
      MOV(W1, reg.toW());
    }
    // mov(eax, dword[ebx]);
    LDR(W16, X1);
  } else {
    // Old-style resolve.
    // Not too important because indirection table is almost always available.
    // mov(rcx, GetContextReg());
    // mov(edx, reg.cvt32());
    //
    // mov(rax, reinterpret_cast<uint64_t>(ResolveFunction));
    // call(rax);
    MOV(X0, GetContextReg());
    MOV(W1, reg.toW());

    ADRP(X16, ResolveFunction);
    BLR(X16);
    MOV(X16, X0);
  }

  // Actually jump/call to X16.
  if (instr->flags & hir::CALL_TAIL) {
    // Since we skip the prolog we need to mark the return here.
    EmitTraceUserCallReturn();

    // Pass the callers return address over.
    // mov(rcx, qword[rsp + StackLayout::GUEST_RET_ADDR]);
    LDR(X0, SP, StackLayout::GUEST_RET_ADDR);

    // add(rsp, static_cast<uint32_t>(stack_size()));
    ADD(SP, SP, static_cast<uint32_t>(stack_size()));

    // jmp(rax);
    BR(X16);
  } else {
    // Return address is from the previous SET_RETURN_ADDRESS.
    // mov(rcx, qword[rsp + StackLayout::GUEST_CALL_RET_ADDR]);
    // call(rax);
    LDR(X0, SP, StackLayout::GUEST_CALL_RET_ADDR);

    BLR(X16);
  }
}

uint64_t UndefinedCallExtern(void* raw_context, uint64_t function_ptr) {
  auto function = reinterpret_cast<Function*>(function_ptr);
  if (!cvars::ignore_undefined_externs) {
    xe::FatalError(fmt::format("undefined extern call to {:08X} {}",
                               function->address(), function->name().c_str()));
  } else {
    XELOGE("undefined extern call to {:08X} {}", function->address(),
           function->name());
  }
  return 0;
}
void A64Emitter::CallExtern(const hir::Instr* instr, const Function* function) {
  bool undefined = true;
  if (function->behavior() == Function::Behavior::kBuiltin) {
    auto builtin_function = static_cast<const BuiltinFunction*>(function);
    if (builtin_function->handler()) {
      undefined = false;
      // x0 = target function
      // x1 = arg0
      // x2 = arg1
      // x3 = arg2
      // mov(rax, reinterpret_cast<uint64_t>(thunk));
      // mov(rcx, reinterpret_cast<uint64_t>(builtin_function->handler()));
      // mov(rdx, reinterpret_cast<uint64_t>(builtin_function->arg0()));
      // mov(r8, reinterpret_cast<uint64_t>(builtin_function->arg1()));
      // call(rax);
      MOV(X0, reinterpret_cast<uint64_t>(builtin_function->handler()));
      MOV(X1, reinterpret_cast<uint64_t>(builtin_function->arg0()));
      MOV(X2, reinterpret_cast<uint64_t>(builtin_function->arg1()));

      auto thunk = backend()->guest_to_host_thunk();
      MOV(X16, reinterpret_cast<uint64_t>(thunk));

      BLR(X16);

      // x0 = host return
    }
  } else if (function->behavior() == Function::Behavior::kExtern) {
    auto extern_function = static_cast<const GuestFunction*>(function);
    if (extern_function->extern_handler()) {
      undefined = false;
      // x0 = target function
      // x1 = arg0
      // x2 = arg1
      // x3 = arg2
      auto thunk = backend()->guest_to_host_thunk();
      // mov(rax, reinterpret_cast<uint64_t>(thunk));
      // mov(rcx,
      // reinterpret_cast<uint64_t>(extern_function->extern_handler()));
      // mov(rdx,
      //     qword[GetContextReg() + offsetof(ppc::PPCContext, kernel_state)]);
      // call(rax);
      MOV(X0, reinterpret_cast<uint64_t>(thunk));
      MOV(X1, reinterpret_cast<uint64_t>(extern_function->extern_handler()));
      LDR(X2, GetContextReg(), offsetof(ppc::PPCContext, kernel_state));

      MOV(X16, reinterpret_cast<uint64_t>(thunk));

      BLR(X16);

      // x0 = host return
    }
  }
  if (undefined) {
    CallNative(UndefinedCallExtern, reinterpret_cast<uint64_t>(function));
  }
}

void A64Emitter::CallNative(void* fn) { CallNativeSafe(fn); }

void A64Emitter::CallNative(uint64_t (*fn)(void* raw_context)) {
  CallNativeSafe(reinterpret_cast<void*>(fn));
}

void A64Emitter::CallNative(uint64_t (*fn)(void* raw_context, uint64_t arg0)) {
  CallNativeSafe(reinterpret_cast<void*>(fn));
}

void A64Emitter::CallNative(uint64_t (*fn)(void* raw_context, uint64_t arg0),
                            uint64_t arg0) {
  // mov(GetNativeParam(0), arg0);
  MOV(GetNativeParam(0), arg0);
  CallNativeSafe(reinterpret_cast<void*>(fn));
}

void A64Emitter::CallNativeSafe(void* fn) {
  // X0 = target function
  // X1 = arg0
  // X2 = arg1
  // X3 = arg2
  auto thunk = backend()->guest_to_host_thunk();

  MOV(X0, reinterpret_cast<uint64_t>(fn));

  MOV(X16, reinterpret_cast<uint64_t>(thunk));
  BLR(X16);

  // X0 = host return
}

void A64Emitter::SetReturnAddress(uint64_t value) {
  // mov(rax, value);
  // mov(qword[rsp + StackLayout::GUEST_CALL_RET_ADDR], rax);
  MOV(X0, value);
  STR(X0, SP, StackLayout::GUEST_CALL_RET_ADDR);
}

oaknut::XReg A64Emitter::GetNativeParam(uint32_t param) {
  if (param == 0)
    return X1;
  else if (param == 1)
    return X2;
  else if (param == 2)
    return X3;

  assert_always();
  return X3;
}

// Important: If you change these, you must update the thunks in a64_backend.cc!
oaknut::XReg A64Emitter::GetContextReg() { return X27; }
oaknut::XReg A64Emitter::GetMembaseReg() { return X28; }

void A64Emitter::ReloadContext() {
  // mov(GetContextReg(), qword[rsp + StackLayout::GUEST_CTX_HOME]);
  LDR(GetContextReg(), SP, StackLayout::GUEST_CTX_HOME);
}

void A64Emitter::ReloadMembase() {
  // mov(GetMembaseReg(), qword[GetContextReg() + 8]);  // membase
  LDR(GetMembaseReg(), GetContextReg(), 8);  // membase
}

bool A64Emitter::ConstantFitsIn32Reg(uint64_t v) {
  if ((v & ~0x7FFFFFFF) == 0) {
    // Fits under 31 bits, so just load using normal mov.
    return true;
  } else if ((v & ~0x7FFFFFFF) == ~0x7FFFFFFF) {
    // Negative number that fits in 32bits.
    return true;
  }
  return false;
}

void A64Emitter::MovMem64(const oaknut::XRegSp& addr, intptr_t offset,
                          uint64_t v) {
  // if ((v & ~0x7FFFFFFF) == 0) {
  //   // Fits under 31 bits, so just load using normal mov.
  //   mov(qword[addr], v);
  // } else if ((v & ~0x7FFFFFFF) == ~0x7FFFFFFF) {
  //   // Negative number that fits in 32bits.
  //   mov(qword[addr], v);
  // } else if (!(v >> 32)) {
  //   // All high bits are zero. It'd be nice if we had a way to load a 32bit
  //   // immediate without sign extending!
  //   // TODO(benvanik): this is super common, find a better way.
  //   mov(dword[addr], static_cast<uint32_t>(v));
  //   mov(dword[addr + 4], 0);
  // } else
  {
    // 64bit number that needs double movs.
    MOV(X0, v);
    STR(X0, addr, offset);
  }
}

static const vec128_t xmm_consts[] = {
    /* VZero                */ vec128f(0.0f),
    /* VOne                 */ vec128f(1.0f),
    /* VOnePD               */ vec128d(1.0),
    /* VNegativeOne         */ vec128f(-1.0f, -1.0f, -1.0f, -1.0f),
    /* VFFFF                */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu),
    /* VMaskX16Y16          */
    vec128i(0x0000FFFFu, 0xFFFF0000u, 0x00000000u, 0x00000000u),
    /* VFlipX16Y16          */
    vec128i(0x00008000u, 0x00000000u, 0x00000000u, 0x00000000u),
    /* VFixX16Y16           */ vec128f(-32768.0f, 0.0f, 0.0f, 0.0f),
    /* VNormalizeX16Y16     */
    vec128f(1.0f / 32767.0f, 1.0f / (32767.0f * 65536.0f), 0.0f, 0.0f),
    /* V0001                */ vec128f(0.0f, 0.0f, 0.0f, 1.0f),
    /* V3301                */ vec128f(3.0f, 3.0f, 0.0f, 1.0f),
    /* V3331                */ vec128f(3.0f, 3.0f, 3.0f, 1.0f),
    /* V3333                */ vec128f(3.0f, 3.0f, 3.0f, 3.0f),
    /* VSignMaskPS          */
    vec128i(0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u),
    /* VSignMaskPD          */
    vec128i(0x00000000u, 0x80000000u, 0x00000000u, 0x80000000u),
    /* VAbsMaskPS           */
    vec128i(0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu),
    /* VAbsMaskPD           */
    vec128i(0xFFFFFFFFu, 0x7FFFFFFFu, 0xFFFFFFFFu, 0x7FFFFFFFu),
    /* VByteSwapMask        */
    vec128i(0x00010203u, 0x04050607u, 0x08090A0Bu, 0x0C0D0E0Fu),
    /* VByteOrderMask       */
    vec128i(0x01000302u, 0x05040706u, 0x09080B0Au, 0x0D0C0F0Eu),
    /* VPermuteControl15    */ vec128b(15),
    /* VPermuteByteMask     */ vec128b(0x1F),
    /* VPackD3DCOLORSat     */ vec128i(0x404000FFu),
    /* VPackD3DCOLOR        */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x0C000408u),
    /* VUnpackD3DCOLOR      */
    vec128i(0xFFFFFF0Eu, 0xFFFFFF0Du, 0xFFFFFF0Cu, 0xFFFFFF0Fu),
    /* VPackFLOAT16_2       */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x01000302u),
    /* VUnpackFLOAT16_2     */
    vec128i(0x0D0C0F0Eu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu),
    /* VPackFLOAT16_4       */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0x01000302u, 0x05040706u),
    /* VUnpackFLOAT16_4     */
    vec128i(0x09080B0Au, 0x0D0C0F0Eu, 0xFFFFFFFFu, 0xFFFFFFFFu),
    /* VPackSHORT_Min       */ vec128i(0x403F8001u),
    /* VPackSHORT_Max       */ vec128i(0x40407FFFu),
    /* VPackSHORT_2         */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x01000504u),
    /* VPackSHORT_4         */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0x01000504u, 0x09080D0Cu),
    /* VUnpackSHORT_2       */
    vec128i(0xFFFF0F0Eu, 0xFFFF0D0Cu, 0xFFFFFFFFu, 0xFFFFFFFFu),
    /* VUnpackSHORT_4       */
    vec128i(0xFFFF0B0Au, 0xFFFF0908u, 0xFFFF0F0Eu, 0xFFFF0D0Cu),
    /* VUnpackSHORT_Overflow */ vec128i(0x403F8000u),
    /* VPackUINT_2101010_MinUnpacked */
    vec128i(0x403FFE01u, 0x403FFE01u, 0x403FFE01u, 0x40400000u),
    /* VPackUINT_2101010_MaxUnpacked */
    vec128i(0x404001FFu, 0x404001FFu, 0x404001FFu, 0x40400003u),
    /* VPackUINT_2101010_MaskUnpacked */
    vec128i(0x3FFu, 0x3FFu, 0x3FFu, 0x3u),
    /* VPackUINT_2101010_MaskPacked */
    vec128i(0x3FFu, 0x3FFu << 10, 0x3FFu << 20, 0x3u << 30),
    /* VPackUINT_2101010_Shift */ vec128i(0, 10, 20, 30),
    /* VUnpackUINT_2101010_Overflow */ vec128i(0x403FFE00u),
    /* VPackULONG_4202020_MinUnpacked */
    vec128i(0x40380001u, 0x40380001u, 0x40380001u, 0x40400000u),
    /* VPackULONG_4202020_MaxUnpacked */
    vec128i(0x4047FFFFu, 0x4047FFFFu, 0x4047FFFFu, 0x4040000Fu),
    /* VPackULONG_4202020_MaskUnpacked */
    vec128i(0xFFFFFu, 0xFFFFFu, 0xFFFFFu, 0xFu),
    /* VPackULONG_4202020_PermuteXZ */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0x0A0908FFu, 0xFF020100u),
    /* VPackULONG_4202020_PermuteYW */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0x0CFFFF06u, 0x0504FFFFu),
    /* VUnpackULONG_4202020_Permute */
    vec128i(0xFF0E0D0Cu, 0xFF0B0A09u, 0xFF080F0Eu, 0xFFFFFF0Bu),
    /* VUnpackULONG_4202020_Overflow */ vec128i(0x40380000u),
    /* VOneOver255          */ vec128f(1.0f / 255.0f),
    /* VMaskEvenPI16        */
    vec128i(0x0000FFFFu, 0x0000FFFFu, 0x0000FFFFu, 0x0000FFFFu),
    /* VShiftMaskEvenPI16   */
    vec128i(0x0000000Fu, 0x0000000Fu, 0x0000000Fu, 0x0000000Fu),
    /* VShiftMaskPS         */
    vec128i(0x0000001Fu, 0x0000001Fu, 0x0000001Fu, 0x0000001Fu),
    /* VShiftByteMask       */
    vec128i(0x000000FFu, 0x000000FFu, 0x000000FFu, 0x000000FFu),
    /* VSwapWordMask        */
    vec128i(0x03030303u, 0x03030303u, 0x03030303u, 0x03030303u),
    /* VUnsignedDwordMax    */
    vec128i(0xFFFFFFFFu, 0x00000000u, 0xFFFFFFFFu, 0x00000000u),
    /* V255                 */ vec128f(255.0f),
    /* VPI32                */ vec128i(32),
    /* VSignMaskI8          */
    vec128i(0x80808080u, 0x80808080u, 0x80808080u, 0x80808080u),
    /* VSignMaskI16         */
    vec128i(0x80008000u, 0x80008000u, 0x80008000u, 0x80008000u),
    /* VSignMaskI32         */
    vec128i(0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u),
    /* VSignMaskF32         */
    vec128i(0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u),
    /* VShortMinPS          */ vec128f(SHRT_MIN),
    /* VShortMaxPS          */ vec128f(SHRT_MAX),
    /* VIntMin              */ vec128i(INT_MIN),
    /* VIntMax              */ vec128i(INT_MAX),
    /* VIntMaxPD            */ vec128d(INT_MAX),
    /* VPosIntMinPS         */ vec128f((float)0x80000000u),
    /* VQNaN                */ vec128i(0x7FC00000u),
    /* VInt127              */ vec128i(0x7Fu),
    /* V2To32               */ vec128f(0x1.0p32f),
};

// First location to try and place constants.
static const uintptr_t kConstDataLocation = 0x20000000;
static const uintptr_t kConstDataSize = sizeof(xmm_consts);

// Increment the location by this amount for every allocation failure.
static const uintptr_t kConstDataIncrement = 0x00001000;

// This function places constant data that is used by the emitter later on.
// Only called once and used by multiple instances of the emitter.
//
// TODO(DrChat): This should be placed in the code cache with the code, but
// doing so requires RIP-relative addressing, which is difficult to support
// given the current setup.
uintptr_t A64Emitter::PlaceConstData() {
  uint8_t* ptr = reinterpret_cast<uint8_t*>(kConstDataLocation);
  void* mem = nullptr;
  while (!mem) {
    mem = memory::AllocFixed(
        ptr, xe::round_up(kConstDataSize, memory::page_size()),
        memory::AllocationType::kReserveCommit, memory::PageAccess::kReadWrite);

    ptr += kConstDataIncrement;
  }

  // The pointer must not be greater than 31 bits.
  assert_zero(reinterpret_cast<uintptr_t>(mem) & ~0x7FFFFFFF);
  std::memcpy(mem, xmm_consts, sizeof(xmm_consts));
  memory::Protect(mem, kConstDataSize, memory::PageAccess::kReadOnly, nullptr);

  return reinterpret_cast<uintptr_t>(mem);
}

void A64Emitter::FreeConstData(uintptr_t data) {
  memory::DeallocFixed(reinterpret_cast<void*>(data), 0,
                       memory::DeallocationType::kRelease);
}

std::byte* A64Emitter::GetVConstPtr(VConst id) {
  // Load through fixed constant table setup by PlaceConstData.
  // It's important that the pointer is not signed, as it will be sign-extended.
  return reinterpret_cast<std::byte*>(backend_->emitter_data() +
                                      sizeof(vec128_t) * id);
}

// Implies possible StashV(0, ...)!
void A64Emitter::LoadConstantV(oaknut::QReg dest, const vec128_t& v) {
  // https://www.agner.org/optimize/optimizing_assembly.pdf
  // 13.4 Generating constants
  if (!v.low && !v.high) {
    // 0000...
    EOR(dest.B16(), dest.B16(), dest.B16());
  }
  // else if (v.low == ~uint64_t(0) && v.high == ~uint64_t(0)) {
  //   // 1111...
  //   vpcmpeqb(dest, dest);
  // }
  else {
    // TODO(benvanik): see what other common values are.
    // TODO(benvanik): build constant table - 99% are reused.
    MovMem64(SP, kStashOffset, v.low);
    MovMem64(SP, kStashOffset + 8, v.high);
    LDR(dest, SP, kStashOffset);
  }
}

void A64Emitter::LoadConstantV(oaknut::QReg dest, float v) {
  union {
    float f;
    uint32_t i;
  } x = {v};
  if (!x.i) {
    // +0.0f (but not -0.0f because it may be used to flip the sign via xor).
    EOR(dest.B16(), dest.B16(), dest.B16());
  }
  // else if (x.i == ~uint32_t(0)) {
  //   // 1111...
  //   vpcmpeqb(dest, dest);
  // }
  else {
    // TODO(benvanik): see what other common values are.
    // TODO(benvanik): build constant table - 99% are reused.
    MOV(W0, x.i);
    MOV(dest.Selem()[0], W0);
  }
}

void A64Emitter::LoadConstantV(oaknut::QReg dest, double v) {
  union {
    double d;
    uint64_t i;
  } x = {v};
  if (!x.i) {
    // +0.0 (but not -0.0 because it may be used to flip the sign via xor).
    EOR(dest.B16(), dest.B16(), dest.B16());
  }
  // else if (x.i == ~uint64_t(0)) {
  //   // 1111...
  //   vpcmpeqb(dest, dest);
  // }
  else {
    // TODO(benvanik): see what other common values are.
    // TODO(benvanik): build constant table - 99% are reused.
    MOV(X0, x.i);
    MOV(dest.Delem()[0], X0);
  }
}

uintptr_t A64Emitter::StashV(int index, const oaknut::QReg& r) {
  // auto addr = ptr[rsp + kStashOffset + (index * 16)];
  // vmovups(addr, r);
  const auto addr = kStashOffset + (index * 16);
  STR(r, SP, addr);
  return addr;
}

uintptr_t A64Emitter::StashConstantV(int index, float v) {
  union {
    float f;
    uint32_t i;
  } x = {v};
  const auto addr = kStashOffset + (index * 16);
  MovMem64(SP, addr, x.i);
  MovMem64(SP, addr + 8, 0);
  return addr;
}

uintptr_t A64Emitter::StashConstantV(int index, double v) {
  union {
    double d;
    uint64_t i;
  } x = {v};
  const auto addr = kStashOffset + (index * 16);
  MovMem64(SP, addr, x.i);
  MovMem64(SP, addr + 8, 0);
  return addr;
}

uintptr_t A64Emitter::StashConstantV(int index, const vec128_t& v) {
  const auto addr = kStashOffset + (index * 16);
  MovMem64(SP, addr, v.low);
  MovMem64(SP, addr + 8, v.high);
  return addr;
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
