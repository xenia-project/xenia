/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/x64/x64_emitter.h"

#include <gflags/gflags.h>

#include <stddef.h>
#include <climits>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/atomic.h"
#include "xenia/base/debugging.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/base/vec128.h"
#include "xenia/cpu/backend/x64/x64_backend.h"
#include "xenia/cpu/backend/x64/x64_code_cache.h"
#include "xenia/cpu/backend/x64/x64_function.h"
#include "xenia/cpu/backend/x64/x64_sequences.h"
#include "xenia/cpu/backend/x64/x64_stack_layout.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/function_debug_info.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/symbol.h"
#include "xenia/cpu/thread_state.h"

DEFINE_bool(enable_debugprint_log, false,
            "Log debugprint traps to the active debugger");
DEFINE_bool(ignore_undefined_externs, true,
            "Don't exit when an undefined extern is called.");
DEFINE_bool(emit_source_annotations, false,
            "Add extra movs and nops to make disassembly easier to read.");

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

using xe::cpu::hir::HIRBuilder;
using xe::cpu::hir::Instr;

static const size_t kMaxCodeSize = 1 * 1024 * 1024;

static const size_t kStashOffset = 32;
// static const size_t kStashOffsetHigh = 32 + 32;

const uint32_t X64Emitter::gpr_reg_map_[X64Emitter::GPR_COUNT] = {
    Xbyak::Operand::RBX, Xbyak::Operand::R10, Xbyak::Operand::R11,
    Xbyak::Operand::R12, Xbyak::Operand::R13, Xbyak::Operand::R14,
    Xbyak::Operand::R15,
};

const uint32_t X64Emitter::xmm_reg_map_[X64Emitter::XMM_COUNT] = {
    4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

X64Emitter::X64Emitter(X64Backend* backend, XbyakAllocator* allocator)
    : CodeGenerator(kMaxCodeSize, Xbyak::AutoGrow, allocator),
      processor_(backend->processor()),
      backend_(backend),
      code_cache_(backend->code_cache()),
      allocator_(allocator) {
  if (FLAGS_enable_haswell_instructions) {
    feature_flags_ |= cpu_.has(Xbyak::util::Cpu::tAVX2) ? kX64EmitAVX2 : 0;
    feature_flags_ |= cpu_.has(Xbyak::util::Cpu::tFMA) ? kX64EmitFMA : 0;
    feature_flags_ |= cpu_.has(Xbyak::util::Cpu::tLZCNT) ? kX64EmitLZCNT : 0;
    feature_flags_ |= cpu_.has(Xbyak::util::Cpu::tBMI2) ? kX64EmitBMI2 : 0;
    feature_flags_ |= cpu_.has(Xbyak::util::Cpu::tF16C) ? kX64EmitF16C : 0;
    feature_flags_ |= cpu_.has(Xbyak::util::Cpu::tMOVBE) ? kX64EmitMovbe : 0;
  }

  if (!cpu_.has(Xbyak::util::Cpu::tAVX)) {
    xe::FatalError(
        "Your CPU does not support AVX, which is required by Xenia. See the "
        "FAQ for system requirements at https://xenia.jp");
    return;
  }
}

X64Emitter::~X64Emitter() = default;

bool X64Emitter::Emit(GuestFunction* function, HIRBuilder* builder,
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
  size_t stack_size = 0;
  if (!Emit(builder, &stack_size)) {
    return false;
  }

  // Copy the final code to the cache and relocate it.
  *out_code_size = getSize();
  *out_code_address = Emplace(stack_size, function);

  // Stash source map.
  source_map_arena_.CloneContents(out_source_map);

  return true;
}

void* X64Emitter::Emplace(size_t stack_size, GuestFunction* function) {
  // To avoid changing xbyak, we do a switcharoo here.
  // top_ points to the Xbyak buffer, and since we are in AutoGrow mode
  // it has pending relocations. We copy the top_ to our buffer, swap the
  // pointer, relocate, then return the original scratch pointer for use.
  uint8_t* old_address = top_;
  void* new_address;
  if (function) {
    new_address = code_cache_->PlaceGuestCode(function->address(), top_, size_,
                                              stack_size, function);
  } else {
    new_address = code_cache_->PlaceHostCode(0, top_, size_, stack_size);
  }
  top_ = reinterpret_cast<uint8_t*>(new_address);
  ready();
  top_ = old_address;
  reset();
  return new_address;
}

bool X64Emitter::Emit(HIRBuilder* builder, size_t* out_stack_size) {
  Xbyak::Label epilog_label;
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

  // Function prolog.
  // Must be 16b aligned.
  // Windows is very strict about the form of this and the epilog:
  // https://docs.microsoft.com/en-us/cpp/build/prolog-and-epilog?view=vs-2017
  // IMPORTANT: any changes to the prolog must be kept in sync with
  //     X64CodeCache, which dynamically generates exception information.
  //     Adding or changing anything here must be matched!
  const size_t stack_size = StackLayout::GUEST_STACK_SIZE + stack_offset;
  assert_true((stack_size + 8) % 16 == 0);
  *out_stack_size = stack_size;
  stack_size_ = stack_size;

  sub(rsp, (uint32_t)stack_size);
  mov(qword[rsp + StackLayout::GUEST_CTX_HOME], GetContextReg());
  mov(qword[rsp + StackLayout::GUEST_RET_ADDR], rcx);
  mov(qword[rsp + StackLayout::GUEST_CALL_RET_ADDR], 0);

  // Safe now to do some tracing.
  if (debug_info_flags_ & DebugInfoFlags::kDebugInfoTraceFunctions) {
    // We require 32-bit addresses.
    assert_true(uint64_t(trace_data_->header()) < UINT_MAX);
    auto trace_header = trace_data_->header();

    // Call count.
    lock();
    inc(qword[low_address(&trace_header->function_call_count)]);

    // Get call history slot.
    static_assert(FunctionTraceData::kFunctionCallerHistoryCount == 4,
                  "bitmask depends on count");
    mov(rax, qword[low_address(&trace_header->function_call_count)]);
    and_(rax, 0b00000011);

    // Record call history value into slot (guest addr in RDX).
    mov(dword[Xbyak::RegExp(uint32_t(uint64_t(
                  low_address(&trace_header->function_caller_history)))) +
              rax * 4],
        edx);

    // Calling thread. Load ax with thread ID.
    EmitGetCurrentThreadId();
    lock();
    bts(qword[low_address(&trace_header->function_thread_use)], rax);
  }

  // Load membase.
  mov(GetMembaseReg(),
      qword[GetContextReg() + offsetof(ppc::PPCContext, virtual_membase)]);

  // Body.
  auto block = builder->first_block();
  while (block) {
    // Mark block labels.
    auto label = block->label_head;
    while (label) {
      L(label->name);
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
        XELOGE("Unable to process HIR opcode %s", instr->opcode->name);
        break;
      }
      instr = new_tail;
    }

    block = block->next;
  }

  // Function epilog.
  L(epilog_label);
  epilog_label_ = nullptr;
  EmitTraceUserCallReturn();
  mov(GetContextReg(), qword[rsp + StackLayout::GUEST_CTX_HOME]);
  add(rsp, (uint32_t)stack_size);
  ret();

  if (FLAGS_emit_source_annotations) {
    nop();
    nop();
    nop();
    nop();
    nop();
  }

  return true;
}

void X64Emitter::MarkSourceOffset(const Instr* i) {
  auto entry = source_map_arena_.Alloc<SourceMapEntry>();
  entry->guest_address = static_cast<uint32_t>(i->src1.offset);
  entry->hir_offset = uint32_t(i->block->ordinal << 16) | i->ordinal;
  entry->code_offset = static_cast<uint32_t>(getSize());

  if (FLAGS_emit_source_annotations) {
    nop();
    nop();
    mov(eax, entry->guest_address);
    nop();
    nop();
  }

  if (debug_info_flags_ & DebugInfoFlags::kDebugInfoTraceFunctionCoverage) {
    uint32_t instruction_index =
        (entry->guest_address - trace_data_->start_address()) / 4;
    lock();
    inc(qword[low_address(trace_data_->instruction_execute_counts() +
                          instruction_index * 8)]);
  }
}

void X64Emitter::EmitGetCurrentThreadId() {
  // rsi must point to context. We could fetch from the stack if needed.
  mov(ax, word[GetContextReg() + offsetof(ppc::PPCContext, thread_id)]);
}

void X64Emitter::EmitTraceUserCallReturn() {}

void X64Emitter::DebugBreak() {
  // TODO(benvanik): notify debugger.
  db(0xCC);
}

uint64_t TrapDebugPrint(void* raw_context, uint64_t address) {
  auto thread_state = *reinterpret_cast<ThreadState**>(raw_context);
  uint32_t str_ptr = uint32_t(thread_state->context()->r[3]);
  // uint16_t str_len = uint16_t(thread_state->context()->r[4]);
  auto str = thread_state->memory()->TranslateVirtual<const char*>(str_ptr);
  // TODO(benvanik): truncate to length?
  XELOGD("(DebugPrint) %s", str);

  if (FLAGS_enable_debugprint_log) {
    debugging::DebugPrint("(DebugPrint) %s", str);
  }

  return 0;
}

uint64_t TrapDebugBreak(void* raw_context, uint64_t address) {
  auto thread_state = *reinterpret_cast<ThreadState**>(raw_context);
  XELOGE("tw/td forced trap hit! This should be a crash!");
  if (FLAGS_break_on_debugbreak) {
    xe::debugging::Break();
  }
  return 0;
}

void X64Emitter::Trap(uint16_t trap_type) {
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
      XELOGW("Unknown trap type %d", trap_type);
      db(0xCC);
      break;
  }
}

void X64Emitter::UnimplementedInstr(const hir::Instr* i) {
  // TODO(benvanik): notify debugger.
  db(0xCC);
  assert_always();
}

// This is used by the X64ThunkEmitter's ResolveFunctionThunk.
extern "C" uint64_t ResolveFunction(void* raw_context,
                                    uint64_t target_address) {
  auto thread_state = *reinterpret_cast<ThreadState**>(raw_context);

  // TODO(benvanik): required?
  assert_not_zero(target_address);

  auto fn =
      thread_state->processor()->ResolveFunction((uint32_t)target_address);
  assert_not_null(fn);
  auto x64_fn = static_cast<X64Function*>(fn);
  uint64_t addr = reinterpret_cast<uint64_t>(x64_fn->machine_code());

  return addr;
}

void X64Emitter::Call(const hir::Instr* instr, GuestFunction* function) {
  assert_not_null(function);
  auto fn = static_cast<X64Function*>(function);
  // Resolve address to the function to call and store in rax.
  if (fn->machine_code()) {
    // TODO(benvanik): is it worth it to do this? It removes the need for
    // a ResolveFunction call, but makes the table less useful.
    assert_zero(uint64_t(fn->machine_code()) & 0xFFFFFFFF00000000);
    mov(eax, uint32_t(uint64_t(fn->machine_code())));
  } else if (code_cache_->has_indirection_table()) {
    // Load the pointer to the indirection table maintained in X64CodeCache.
    // The target dword will either contain the address of the generated code
    // or a thunk to ResolveAddress.
    mov(ebx, function->address());
    mov(eax, dword[ebx]);
  } else {
    // Old-style resolve.
    // Not too important because indirection table is almost always available.
    // TODO: Overwrite the call-site with a straight call.
    CallNative(&ResolveFunction, function->address());
  }

  // Actually jump/call to rax.
  if (instr->flags & hir::CALL_TAIL) {
    // Since we skip the prolog we need to mark the return here.
    EmitTraceUserCallReturn();

    // Pass the callers return address over.
    mov(rcx, qword[rsp + StackLayout::GUEST_RET_ADDR]);

    add(rsp, static_cast<uint32_t>(stack_size()));
    jmp(rax);
  } else {
    // Return address is from the previous SET_RETURN_ADDRESS.
    mov(rcx, qword[rsp + StackLayout::GUEST_CALL_RET_ADDR]);

    call(rax);
  }
}

void X64Emitter::CallIndirect(const hir::Instr* instr,
                              const Xbyak::Reg64& reg) {
  // Check if return.
  if (instr->flags & hir::CALL_POSSIBLE_RETURN) {
    cmp(reg.cvt32(), dword[rsp + StackLayout::GUEST_RET_ADDR]);
    je(epilog_label(), CodeGenerator::T_NEAR);
  }

  // Load the pointer to the indirection table maintained in X64CodeCache.
  // The target dword will either contain the address of the generated code
  // or a thunk to ResolveAddress.
  if (code_cache_->has_indirection_table()) {
    if (reg.cvt32() != ebx) {
      mov(ebx, reg.cvt32());
    }
    mov(eax, dword[ebx]);
  } else {
    // Old-style resolve.
    // Not too important because indirection table is almost always available.
    mov(edx, reg.cvt32());
    mov(rax, reinterpret_cast<uint64_t>(ResolveFunction));
    mov(rcx, GetContextReg());
    call(rax);
  }

  // Actually jump/call to rax.
  if (instr->flags & hir::CALL_TAIL) {
    // Since we skip the prolog we need to mark the return here.
    EmitTraceUserCallReturn();

    // Pass the callers return address over.
    mov(rcx, qword[rsp + StackLayout::GUEST_RET_ADDR]);

    add(rsp, static_cast<uint32_t>(stack_size()));
    jmp(rax);
  } else {
    // Return address is from the previous SET_RETURN_ADDRESS.
    mov(rcx, qword[rsp + StackLayout::GUEST_CALL_RET_ADDR]);

    call(rax);
  }
}

uint64_t UndefinedCallExtern(void* raw_context, uint64_t function_ptr) {
  auto function = reinterpret_cast<Function*>(function_ptr);
  if (!FLAGS_ignore_undefined_externs) {
    xe::FatalError("undefined extern call to %.8X %s", function->address(),
                   function->name().c_str());
  } else {
    XELOGE("undefined extern call to %.8X %s", function->address(),
           function->name().c_str());
  }
  return 0;
}
void X64Emitter::CallExtern(const hir::Instr* instr, const Function* function) {
  bool undefined = true;
  if (function->behavior() == Function::Behavior::kBuiltin) {
    auto builtin_function = static_cast<const BuiltinFunction*>(function);
    if (builtin_function->handler()) {
      undefined = false;
      // rcx = target function
      // rdx = arg0
      // r8  = arg1
      // r9  = arg2
      auto thunk = backend()->guest_to_host_thunk();
      mov(rax, reinterpret_cast<uint64_t>(thunk));
      mov(rcx, reinterpret_cast<uint64_t>(builtin_function->handler()));
      mov(rdx, reinterpret_cast<uint64_t>(builtin_function->arg0()));
      mov(r8, reinterpret_cast<uint64_t>(builtin_function->arg1()));
      call(rax);
      // rax = host return
    }
  } else if (function->behavior() == Function::Behavior::kExtern) {
    auto extern_function = static_cast<const GuestFunction*>(function);
    if (extern_function->extern_handler()) {
      undefined = false;
      // rcx = target function
      // rdx = arg0
      // r8  = arg1
      // r9  = arg2
      auto thunk = backend()->guest_to_host_thunk();
      mov(rax, reinterpret_cast<uint64_t>(thunk));
      mov(rcx, reinterpret_cast<uint64_t>(extern_function->extern_handler()));
      mov(rdx,
          qword[GetContextReg() + offsetof(ppc::PPCContext, kernel_state)]);
      call(rax);
      // rax = host return
    }
  }
  if (undefined) {
    CallNative(UndefinedCallExtern, reinterpret_cast<uint64_t>(function));
  }
}

void X64Emitter::CallNative(void* fn) { CallNativeSafe(fn); }

void X64Emitter::CallNative(uint64_t (*fn)(void* raw_context)) {
  CallNativeSafe(reinterpret_cast<void*>(fn));
}

void X64Emitter::CallNative(uint64_t (*fn)(void* raw_context, uint64_t arg0)) {
  CallNativeSafe(reinterpret_cast<void*>(fn));
}

void X64Emitter::CallNative(uint64_t (*fn)(void* raw_context, uint64_t arg0),
                            uint64_t arg0) {
  mov(GetNativeParam(0), arg0);
  CallNativeSafe(reinterpret_cast<void*>(fn));
}

void X64Emitter::CallNativeSafe(void* fn) {
  // rcx = target function
  // rdx = arg0
  // r8  = arg1
  // r9  = arg2
  auto thunk = backend()->guest_to_host_thunk();
  mov(rax, reinterpret_cast<uint64_t>(thunk));
  mov(rcx, reinterpret_cast<uint64_t>(fn));
  call(rax);
  // rax = host return
}

void X64Emitter::SetReturnAddress(uint64_t value) {
  mov(rax, value);
  mov(qword[rsp + StackLayout::GUEST_CALL_RET_ADDR], rax);
}

Xbyak::Reg64 X64Emitter::GetNativeParam(uint32_t param) {
  if (param == 0)
    return rdx;
  else if (param == 1)
    return r8;
  else if (param == 2)
    return r9;

  assert_always();
  return r9;
}

// Important: If you change these, you must update the thunks in x64_backend.cc!
Xbyak::Reg64 X64Emitter::GetContextReg() { return rsi; }
Xbyak::Reg64 X64Emitter::GetMembaseReg() { return rdi; }

void X64Emitter::ReloadContext() {
  mov(GetContextReg(), qword[rsp + StackLayout::GUEST_CTX_HOME]);
}

void X64Emitter::ReloadMembase() {
  mov(GetMembaseReg(), qword[GetContextReg() + 8]);  // membase
}

// Len Assembly                                   Byte Sequence
// ============================================================================
// 1b  NOP                                        90H
// 2b  66 NOP                                     66 90H
// 3b  NOP DWORD ptr [EAX]                        0F 1F 00H
// 4b  NOP DWORD ptr [EAX + 00H]                  0F 1F 40 00H
// 5b  NOP DWORD ptr [EAX + EAX*1 + 00H]          0F 1F 44 00 00H
// 6b  66 NOP DWORD ptr [EAX + EAX*1 + 00H]       66 0F 1F 44 00 00H
// 7b  NOP DWORD ptr [EAX + 00000000H]            0F 1F 80 00 00 00 00H
// 8b  NOP DWORD ptr [EAX + EAX*1 + 00000000H]    0F 1F 84 00 00 00 00 00H
// 9b  66 NOP DWORD ptr [EAX + EAX*1 + 00000000H] 66 0F 1F 84 00 00 00 00 00H
void X64Emitter::nop(size_t length) {
  // TODO(benvanik): fat nop
  for (size_t i = 0; i < length; ++i) {
    db(0x90);
  }
}

bool X64Emitter::ConstantFitsIn32Reg(uint64_t v) {
  if ((v & ~0x7FFFFFFF) == 0) {
    // Fits under 31 bits, so just load using normal mov.
    return true;
  } else if ((v & ~0x7FFFFFFF) == ~0x7FFFFFFF) {
    // Negative number that fits in 32bits.
    return true;
  }
  return false;
}

void X64Emitter::MovMem64(const Xbyak::RegExp& addr, uint64_t v) {
  if ((v & ~0x7FFFFFFF) == 0) {
    // Fits under 31 bits, so just load using normal mov.
    mov(qword[addr], v);
  } else if ((v & ~0x7FFFFFFF) == ~0x7FFFFFFF) {
    // Negative number that fits in 32bits.
    mov(qword[addr], v);
  } else if (!(v >> 32)) {
    // All high bits are zero. It'd be nice if we had a way to load a 32bit
    // immediate without sign extending!
    // TODO(benvanik): this is super common, find a better way.
    mov(dword[addr], static_cast<uint32_t>(v));
    mov(dword[addr + 4], 0);
  } else {
    // 64bit number that needs double movs.
    mov(dword[addr], static_cast<uint32_t>(v));
    mov(dword[addr + 4], static_cast<uint32_t>(v >> 32));
  }
}

static const vec128_t xmm_consts[] = {
    /* XMMZero                */ vec128f(0.0f),
    /* XMMOne                 */ vec128f(1.0f),
    /* XMMNegativeOne         */ vec128f(-1.0f, -1.0f, -1.0f, -1.0f),
    /* XMMFFFF                */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu),
    /* XMMMaskX16Y16          */
    vec128i(0x0000FFFFu, 0xFFFF0000u, 0x00000000u, 0x00000000u),
    /* XMMFlipX16Y16          */
    vec128i(0x00008000u, 0x00000000u, 0x00000000u, 0x00000000u),
    /* XMMFixX16Y16           */ vec128f(-32768.0f, 0.0f, 0.0f, 0.0f),
    /* XMMNormalizeX16Y16     */
    vec128f(1.0f / 32767.0f, 1.0f / (32767.0f * 65536.0f), 0.0f, 0.0f),
    /* XMM0001                */ vec128f(0.0f, 0.0f, 0.0f, 1.0f),
    /* XMM3301                */ vec128f(3.0f, 3.0f, 0.0f, 1.0f),
    /* XMM3331                */ vec128f(3.0f, 3.0f, 3.0f, 1.0f),
    /* XMM3333                */ vec128f(3.0f, 3.0f, 3.0f, 3.0f),
    /* XMMSignMaskPS          */
    vec128i(0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u),
    /* XMMSignMaskPD          */
    vec128i(0x00000000u, 0x80000000u, 0x00000000u, 0x80000000u),
    /* XMMAbsMaskPS           */
    vec128i(0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu),
    /* XMMAbsMaskPD           */
    vec128i(0xFFFFFFFFu, 0x7FFFFFFFu, 0xFFFFFFFFu, 0x7FFFFFFFu),
    /* XMMByteSwapMask        */
    vec128i(0x00010203u, 0x04050607u, 0x08090A0Bu, 0x0C0D0E0Fu),
    /* XMMByteOrderMask       */
    vec128i(0x01000302u, 0x05040706u, 0x09080B0Au, 0x0D0C0F0Eu),
    /* XMMPermuteControl15    */ vec128b(15),
    /* XMMPermuteByteMask     */ vec128b(0x1F),
    /* XMMPackD3DCOLORSat     */ vec128i(0x404000FFu),
    /* XMMPackD3DCOLOR        */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x0C000408u),
    /* XMMUnpackD3DCOLOR      */
    vec128i(0xFFFFFF0Eu, 0xFFFFFF0Du, 0xFFFFFF0Cu, 0xFFFFFF0Fu),
    /* XMMPackFLOAT16_2       */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x01000302u),
    /* XMMUnpackFLOAT16_2     */
    vec128i(0x0D0C0F0Eu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu),
    /* XMMPackFLOAT16_4       */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0x01000302u, 0x05040706u),
    /* XMMUnpackFLOAT16_4     */
    vec128i(0x09080B0Au, 0x0D0C0F0Eu, 0xFFFFFFFFu, 0xFFFFFFFFu),
    /* XMMPackSHORT_Min       */ vec128i(0x403F8001u),
    /* XMMPackSHORT_Max       */ vec128i(0x40407FFFu),
    /* XMMPackSHORT_2         */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x01000504u),
    /* XMMPackSHORT_4         */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0x01000504u, 0x09080D0Cu),
    /* XMMUnpackSHORT_2       */
    vec128i(0xFFFF0F0Eu, 0xFFFF0D0Cu, 0xFFFFFFFFu, 0xFFFFFFFFu),
    /* XMMUnpackSHORT_4       */
    vec128i(0xFFFF0B0Au, 0xFFFF0908u, 0xFFFF0F0Eu, 0xFFFF0D0Cu),
    /* XMMUnpackSHORT_Overflow */ vec128i(0x403F8000u),
    /* XMMPackUINT_2101010_MinUnpacked */
    vec128i(0x403FFE01u, 0x403FFE01u, 0x403FFE01u, 0x40400000u),
    /* XMMPackUINT_2101010_MaxUnpacked */
    vec128i(0x404001FFu, 0x404001FFu, 0x404001FFu, 0x40400003u),
    /* XMMPackUINT_2101010_MaskUnpacked */
    vec128i(0x3FFu, 0x3FFu, 0x3FFu, 0x3u),
    /* XMMPackUINT_2101010_MaskPacked */
    vec128i(0x3FFu, 0x3FFu << 10, 0x3FFu << 20, 0x3u << 30),
    /* XMMPackUINT_2101010_Shift */ vec128i(0, 10, 20, 30),
    /* XMMUnpackUINT_2101010_Overflow */ vec128i(0x403FFE00u),
    /* XMMUnpackOverflowNaN   */ vec128i(0x7FC00000u),
    /* XMMOneOver255          */ vec128f(1.0f / 255.0f),
    /* XMMMaskEvenPI16        */
    vec128i(0x0000FFFFu, 0x0000FFFFu, 0x0000FFFFu, 0x0000FFFFu),
    /* XMMShiftMaskEvenPI16   */
    vec128i(0x0000000Fu, 0x0000000Fu, 0x0000000Fu, 0x0000000Fu),
    /* XMMShiftMaskPS         */
    vec128i(0x0000001Fu, 0x0000001Fu, 0x0000001Fu, 0x0000001Fu),
    /* XMMShiftByteMask       */
    vec128i(0x000000FFu, 0x000000FFu, 0x000000FFu, 0x000000FFu),
    /* XMMSwapWordMask        */
    vec128i(0x03030303u, 0x03030303u, 0x03030303u, 0x03030303u),
    /* XMMUnsignedDwordMax    */
    vec128i(0xFFFFFFFFu, 0x00000000u, 0xFFFFFFFFu, 0x00000000u),
    /* XMM255                 */ vec128f(255.0f),
    /* XMMPI32                */ vec128i(32),
    /* XMMSignMaskI8          */
    vec128i(0x80808080u, 0x80808080u, 0x80808080u, 0x80808080u),
    /* XMMSignMaskI16         */
    vec128i(0x80008000u, 0x80008000u, 0x80008000u, 0x80008000u),
    /* XMMSignMaskI32         */
    vec128i(0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u),
    /* XMMSignMaskF32         */
    vec128i(0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u),
    /* XMMShortMinPS          */ vec128f(SHRT_MIN),
    /* XMMShortMaxPS          */ vec128f(SHRT_MAX),
    /* XMMIntMin              */ vec128i(INT_MIN),
    /* XMMIntMax              */ vec128i(INT_MAX),
    /* XMMIntMaxPD            */ vec128d(INT_MAX),
    /* XMMPosIntMinPS         */ vec128f((float)0x80000000u),
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
uintptr_t X64Emitter::PlaceConstData() {
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

void X64Emitter::FreeConstData(uintptr_t data) {
  memory::DeallocFixed(reinterpret_cast<void*>(data), 0,
                       memory::DeallocationType::kRelease);
}

Xbyak::Address X64Emitter::GetXmmConstPtr(XmmConst id) {
  // Load through fixed constant table setup by PlaceConstData.
  // It's important that the pointer is not signed, as it will be sign-extended.
  return ptr[reinterpret_cast<void*>(backend_->emitter_data() +
                                     sizeof(vec128_t) * id)];
}

void X64Emitter::LoadConstantXmm(Xbyak::Xmm dest, const vec128_t& v) {
  // https://www.agner.org/optimize/optimizing_assembly.pdf
  // 13.4 Generating constants
  if (!v.low && !v.high) {
    // 0000...
    vpxor(dest, dest);
  } else if (v.low == ~0ull && v.high == ~0ull) {
    // 1111...
    vpcmpeqb(dest, dest);
  } else {
    // TODO(benvanik): see what other common values are.
    // TODO(benvanik): build constant table - 99% are reused.
    MovMem64(rsp + kStashOffset, v.low);
    MovMem64(rsp + kStashOffset + 8, v.high);
    vmovdqa(dest, ptr[rsp + kStashOffset]);
  }
}

void X64Emitter::LoadConstantXmm(Xbyak::Xmm dest, float v) {
  union {
    float f;
    uint32_t i;
  } x = {v};
  if (!v) {
    // 0
    vpxor(dest, dest);
  } else if (x.i == ~0U) {
    // 1111...
    vpcmpeqb(dest, dest);
  } else {
    // TODO(benvanik): see what other common values are.
    // TODO(benvanik): build constant table - 99% are reused.
    mov(eax, x.i);
    vmovd(dest, eax);
  }
}

void X64Emitter::LoadConstantXmm(Xbyak::Xmm dest, double v) {
  union {
    double d;
    uint64_t i;
  } x = {v};
  if (!v) {
    // 0
    vpxor(dest, dest);
  } else if (x.i == ~0ULL) {
    // 1111...
    vpcmpeqb(dest, dest);
  } else {
    // TODO(benvanik): see what other common values are.
    // TODO(benvanik): build constant table - 99% are reused.
    mov(rax, x.i);
    vmovq(dest, rax);
  }
}

Xbyak::Address X64Emitter::StashXmm(int index, const Xbyak::Xmm& r) {
  auto addr = ptr[rsp + kStashOffset + (index * 16)];
  vmovups(addr, r);
  return addr;
}

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
