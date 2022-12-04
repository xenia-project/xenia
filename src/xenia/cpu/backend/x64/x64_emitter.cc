/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/x64/x64_emitter.h"

#include <stddef.h>

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
#include "xenia/cpu/backend/x64/x64_backend.h"
#include "xenia/cpu/backend/x64/x64_code_cache.h"
#include "xenia/cpu/backend/x64/x64_function.h"
#include "xenia/cpu/backend/x64/x64_sequences.h"
#include "xenia/cpu/backend/x64/x64_stack_layout.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/function_debug_info.h"
#include "xenia/cpu/hir/instr.h"
#include "xenia/cpu/hir/opcodes.h"
#include "xenia/cpu/hir/value.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/symbol.h"
#include "xenia/cpu/thread_state.h"

DEFINE_bool(debugprint_trap_log, false,
            "Log debugprint traps to the active debugger", "CPU");
DEFINE_bool(ignore_undefined_externs, true,
            "Don't exit when an undefined extern is called.", "CPU");
DEFINE_bool(emit_source_annotations, false,
            "Add extra movs and nops to make disassembly easier to read.",
            "CPU");

DEFINE_bool(enable_incorrect_roundingmode_behavior, false,
            "Disables the FPU/VMX MXCSR sharing workaround, potentially "
            "causing incorrect rounding behavior and denormal handling in VMX "
            "code. The workaround may cause reduced CPU performance but is a "
            "more accurate emulation",
            "x64");
DEFINE_uint32(align_all_basic_blocks, 0,
              "Aligns the start of all basic blocks to N bytes. Only specify a "
              "power of 2, 16 is the recommended value. Results in larger "
              "icache usage, but potentially faster loops",
              "x64");
#if XE_X64_PROFILER_AVAILABLE == 1
DEFINE_bool(instrument_call_times, false,
            "Compute time taken for functions, for profiling guest code",
            "x64");
#endif
namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

using xe::cpu::hir::HIRBuilder;
using xe::cpu::hir::Instr;
using namespace xe::literals;

static const size_t kMaxCodeSize = 1_MiB;

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
  if (!cpu_.has(Xbyak::util::Cpu::tAVX)) {
    xe::FatalError(
        "Your CPU does not support AVX, which is required by Xenia. See the "
        "FAQ for system requirements at https://xenia.jp");
    return;
  }

  feature_flags_ = amd64::GetFeatureFlags();

  may_use_membase32_as_zero_reg_ =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
          processor()->memory()->virtual_membase())) == 0;
}

X64Emitter::~X64Emitter() = default;

bool X64Emitter::Emit(GuestFunction* function, HIRBuilder* builder,
                      uint32_t debug_info_flags, FunctionDebugInfo* debug_info,
                      void** out_code_address, size_t* out_code_size,
                      std::vector<SourceMapEntry>* out_source_map) {
  SCOPE_profile_cpu_f("cpu");
  guest_module_ = dynamic_cast<XexModule*>(function->module());
  current_guest_function_ = function->address();
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
  *out_code_size = getSize();
  *out_code_address = Emplace(func_info, function);

  // Stash source map.
  source_map_arena_.CloneContents(out_source_map);

  return true;
}
void* X64Emitter::Emplace(const EmitFunctionInfo& func_info,
                          GuestFunction* function) {
  // To avoid changing xbyak, we do a switcharoo here.
  // top_ points to the Xbyak buffer, and since we are in AutoGrow mode
  // it has pending relocations. We copy the top_ to our buffer, swap the
  // pointer, relocate, then return the original scratch pointer for use.
  // top_ is used by Xbyak's ready() as both write base pointer and the absolute
  // address base, which would not work on platforms not supporting writable
  // executable memory, but Xenia doesn't use absolute label addresses in the
  // generated code.
  uint8_t* old_address = top_;
  void* new_execute_address;
  void* new_write_address;
  assert_true(func_info.code_size.total == size_);
  if (function) {
    code_cache_->PlaceGuestCode(function->address(), top_, func_info, function,
                                new_execute_address, new_write_address);
  } else {
    code_cache_->PlaceHostCode(0, top_, func_info, new_execute_address,
                               new_write_address);
  }
  top_ = reinterpret_cast<uint8_t*>(new_write_address);
  ready();
  top_ = old_address;
  reset();
  tail_code_.clear();
  for (auto&& cached_label : label_cache_) {
    delete cached_label;
  }
  label_cache_.clear();
  return new_execute_address;
}

bool X64Emitter::Emit(HIRBuilder* builder, EmitFunctionInfo& func_info) {
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

  struct _code_offsets {
    size_t prolog;
    size_t prolog_stack_alloc;
    size_t body;
    size_t epilog;
    size_t tail;
  } code_offsets = {};

  code_offsets.prolog = getSize();

  // Function prolog.
  // Must be 16b aligned.
  // Windows is very strict about the form of this and the epilog:
  // https://docs.microsoft.com/en-us/cpp/build/prolog-and-epilog?view=vs-2017
  // IMPORTANT: any changes to the prolog must be kept in sync with
  //     X64CodeCache, which dynamically generates exception information.
  //     Adding or changing anything here must be matched!
  const size_t stack_size = StackLayout::GUEST_STACK_SIZE + stack_offset;
  assert_true((stack_size + 8) % 16 == 0);
  func_info.stack_size = stack_size;
  stack_size_ = stack_size;

  PushStackpoint();
  sub(rsp, (uint32_t)stack_size);

  code_offsets.prolog_stack_alloc = getSize();
  code_offsets.body = getSize();
  xor_(eax, eax);
  /*
  * chrispy: removed this, it serves no purpose
  mov(qword[rsp + StackLayout::GUEST_CTX_HOME], GetContextReg());
  */

  mov(qword[rsp + StackLayout::GUEST_RET_ADDR], rcx);

  mov(qword[rsp + StackLayout::GUEST_CALL_RET_ADDR], rax);  // 0

#if XE_X64_PROFILER_AVAILABLE == 1
  if (cvars::instrument_call_times) {
    mov(rdx, 0x7ffe0014);  // load pointer to kusershared systemtime
    mov(rdx, qword[rdx]);
    mov(qword[rsp + StackLayout::GUEST_PROFILER_START],
        rdx);  // save time for end of function
  }
#endif
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
  /*
  * chrispy: removed this, as long as we load it in HostToGuestThunk we can
  count on no other code modifying it. mov(GetMembaseReg(),
      qword[GetContextReg() + offsetof(ppc::PPCContext, virtual_membase)]);
  */
  // Body.
  auto block = builder->first_block();
  synchronize_stack_on_next_instruction_ = false;
  while (block) {
    ForgetMxcsrMode();  // at start of block, mxcsr mode is undefined

    // Mark block labels.
    auto label = block->label_head;
    while (label) {
      L(std::to_string(label->id));
      label = label->next;
    }

    if (cvars::align_all_basic_blocks) {
      align(cvars::align_all_basic_blocks, true);
    }
    // Process instructions.
    const Instr* instr = block->instr_head;
    while (instr) {
      if (synchronize_stack_on_next_instruction_) {
        if (instr->GetOpcodeNum() != hir::OPCODE_SOURCE_OFFSET) {
          synchronize_stack_on_next_instruction_ = false;
          EnsureSynchronizedGuestAndHostStack();
        }
      }
      const Instr* new_tail = instr;
      if (!SelectSequence(this, instr, &new_tail)) {
        // No sequence found!
        // NOTE: If you encounter this after adding a new instruction, do a full
        // rebuild!
        assert_always();
        XELOGE("Unable to process HIR opcode {}", GetOpcodeName(instr->opcode));
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
  /*
  * chrispy: removed this, it serves no purpose
  mov(GetContextReg(), qword[rsp + StackLayout::GUEST_CTX_HOME]);
  */
  code_offsets.epilog = getSize();
  EmitProfilerEpilogue();

  add(rsp, (uint32_t)stack_size);
  PopStackpoint();
  ret();
  // todo: do some kind of sorting by alignment?
  for (auto&& tail_item : tail_code_) {
    if (tail_item.alignment) {
      align(tail_item.alignment);
    }
    tail_item.func(*this, tail_item.label);
  }

  code_offsets.tail = getSize();

  if (cvars::emit_source_annotations) {
    nop(5);
  }

  assert_zero(code_offsets.prolog);
  func_info.code_size.total = getSize();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = getSize() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;

  return true;
}
// dont use rax, we do this in tail call handling
void X64Emitter::EmitProfilerEpilogue() {
#if XE_X64_PROFILER_AVAILABLE == 1
  if (cvars::instrument_call_times) {
    uint64_t* profiler_entry =
        backend()->GetProfilerRecordForFunction(current_guest_function_);

    mov(ecx, 0x7ffe0014);
    mov(rdx, qword[rcx]);
    mov(r10, (uintptr_t)profiler_entry);
    sub(rdx, qword[rsp + StackLayout::GUEST_PROFILER_START]);

    // atomic add our time to the profiler entry
    // this could be atomic free if we had per thread profile counts, and on a
    // threads exit we lock and sum up to the global counts, which would make
    // this a few cycles less intrusive, but its good enough for now
    //  actually... lets just try without atomics lol
    // lock();
    add(qword[r10], rdx);
  }
#endif
}

void X64Emitter::MarkSourceOffset(const Instr* i) {
  auto entry = source_map_arena_.Alloc<SourceMapEntry>();
  entry->guest_address = static_cast<uint32_t>(i->src1.offset);
  entry->hir_offset = uint32_t(i->block->ordinal << 16) | i->ordinal;
  entry->code_offset = static_cast<uint32_t>(getSize());

  if (cvars::emit_source_annotations) {
    nop(2);
    mov(eax, entry->guest_address);
    nop(2);
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
  auto thread_state =
      reinterpret_cast<ppc::PPCContext_s*>(raw_context)->thread_state;
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
  auto thread_state =
      reinterpret_cast<ppc::PPCContext_s*>(raw_context)->thread_state;
  XELOGE("tw/td forced trap hit! This should be a crash!");
  if (cvars::break_on_debugbreak) {
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
      XELOGW("Unknown trap type {}", trap_type);
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
uint64_t ResolveFunction(void* raw_context, uint64_t target_address) {
  auto guest_context = reinterpret_cast<ppc::PPCContext_s*>(raw_context);

  auto thread_state = guest_context->thread_state;

  // TODO(benvanik): required?
  assert_not_zero(target_address);

  /*
          todo: refactor this!

         The purpose of this code is to allow guest longjmp to call into
     the body of an existing host function. There are a lot of conditions we
     have to check here to ensure that we do not mess up a normal call to a
     function

         The address must be within an XexModule (may need to make some changes
     to instructionaddressflags to remove this limitation) The target address
     must be a known return site. The guest address must be part of a function
     that was already translated.

  */

  if (cvars::enable_host_guest_stack_synchronization) {
    auto processor = thread_state->processor();

    auto module_for_address =
        processor->LookupModule(static_cast<uint32_t>(target_address));

    if (module_for_address) {
      XexModule* xexmod = dynamic_cast<XexModule*>(module_for_address);
      if (xexmod) {
        InfoCacheFlags* flags = xexmod->GetInstructionAddressFlags(
            static_cast<uint32_t>(target_address));
        if (flags) {
          if (flags->is_return_site) {
            auto ones_with_address = processor->FindFunctionsWithAddress(
                static_cast<uint32_t>(target_address));

            if (ones_with_address.size() != 0) {
              // this loop to find a host address for the guest address is
              // necessary because FindFunctionsWithAddress works via a range
              // check, but if the function consists of multiple blocks
              // scattered around with "holes" of instructions that cannot be
              // reached in between those holes the instructions that cannot be
              // reached will incorrectly be considered members of the function

              X64Function* candidate = nullptr;
              uintptr_t host_address = 0;
              for (auto&& entry : ones_with_address) {
                X64Function* xfunc = static_cast<X64Function*>(entry);

                host_address = xfunc->MapGuestAddressToMachineCode(
                    static_cast<uint32_t>(target_address));
                // host address does exist within the function, and that host
                // function is not the start of the function, it is instead
                // somewhere within its existing body
                // i originally did not have this (xfunc->machine_code() !=
                // reinterpret_cast<const uint8_t*>(host_address))) condition
                // here when i distributed builds for testing, no issues arose
                // related to it but i wanted to be more explicit
                if (host_address &&
                    xfunc->machine_code() !=
                        reinterpret_cast<const uint8_t*>(host_address)) {
                  candidate = xfunc;
                  break;
                }
              }
              // we found an existing X64Function, and a return site within that
              // function that has a host address w/ native code
              if (candidate && host_address) {
                X64Backend* backend =
                    static_cast<X64Backend*>(processor->backend());
                // grab the backend context, next we have to check whether the
                // guest and host stack are out of sync if they arent, its fine
                // for the backend to create a new function for the guest
                // address we're resolving if they are, it means that the reason
                // we're resolving this address is because context is being
                // restored (probably by longjmp)
                X64BackendContext* backend_context =
                    backend->BackendContextForGuestContext(guest_context);

                uint32_t current_stackpoint_index =
                    backend_context->current_stackpoint_depth;

                --current_stackpoint_index;

                X64BackendStackpoint* stackpoints =
                    backend_context->stackpoints;

                uint32_t current_guest_stackpointer =
                    static_cast<uint32_t>(guest_context->r[1]);
                uint32_t num_frames_bigger = 0;

                /*
                        if the current guest stack pointer is bigger than the
                   recorded pointer for this stack thats fine, plenty of
                   functions restore the original stack pointer early

                        if more than 1... we're longjmping and sure of it at
                   this point (jumping to a return site that has already been
                   emitted)
                */
                while (current_stackpoint_index != 0xFFFFFFFF) {
                  if (current_guest_stackpointer >
                      stackpoints[current_stackpoint_index].guest_stack_) {
                    --current_stackpoint_index;
                    ++num_frames_bigger;

                  } else {
                    break;
                  }
                }
                /*
                                        DEFINITELY a longjmp, return original
                   host address. returning the existing host address is going to
                   set off some extra machinery we have set up to support this

                                        to break it down, our caller (us being
                   this ResolveFunction that this comment is in) is
                   X64Backend::resolve_function_thunk_ which is implemented in
                   x64_backend.cc X64HelperEmitter::EmitResolveFunctionThunk, or
                   a call from the resolver table

                                        the x64 fastcall abi dictates that the
                   stack must always be 16 byte aligned. We select our stack
                   size for functions to ensure that we keep rsp aligned to 16
                   bytes

                                        but by calling into the body of an
                   existing function we've pushed our return address onto the
                   stack (dont worry about this return address, it gets
                   discarded in a later step)

                                        this means that the stack is no longer
                   16 byte aligned, (rsp % 16) now == 8, and this is the only
                   time outside of the prolog or epilog of a function that this
                   will be the case

                                        so, after all direct or indirect
                   function calls we set
                   X64Emitter::synchronize_stack_on_next_instruction_ to true.
                                        On the next instruction that is not
                   OPCODE_SOURCE_OFFSET we will emit a check when we see
                   synchronize_stack_on_next_instruction_ is true. We have to
                   skip OPCODE_SOURCE_OFFSET because its not a "real"
                   instruction and if we emit on it the return address of the
                   function call will point to AFTER our check, so itll never be
                   executed.

                                        our check is just going to do test esp,
                   15 to see if the stack is misaligned. (using esp instead of
                   rsp saves 1 byte). We tail emit the handling for when the
                   check succeeds because in 99.99999% of function calls it will
                   be aligned, in the end the runtime cost of these checks is 5
                   bytes for the test instruction which ought to be one cycle
                   and 5 bytes for the jmp with no cycles taken for the jump
                   which will be predicted not taken.

                  Our handling for the check is implemented in
                  X64HelperEmitter::EmitGuestAndHostSynchronizeStackHelper. we
                  don't call it directly though, instead we go through
                  backend()->synchronize_guest_and_host_stack_helper_for_size(num_bytes_needed_to_represent_stack_size).
                  we place the stack size after the call instruction so we can
                  load it in the helper and readjust the return address to point
                  after the literal value.

                                  The helper is going to search the array of
                  stackpoints to find the first one that is greater than or
                  equal to the current stack pointer, when it finds the entry it
                  will set the currently host rsp to the host stack pointer
                  value in the entry, and then subtract the stack size of the
                  caller from that. the current stackpoint index is adjusted to
                  point to the one after the stackpoint we restored to.

                                  The helper then jumps back to the function
                  that was longjmp'ed to, with the host stack in its proper
                  state. it just works!



                 */

                if (num_frames_bigger > 1) {
                  /*
                   * can't do anything about this right now :(
                   * epic mickey is quite slow due to having to call resolve on
                   * every longjmp, and it longjmps a lot but if we add an
                   * indirection we lose our stack misalignment check
                   */
                  /* reinterpret_cast<X64CodeCache*>(backend->code_cache())
      ->AddIndirection(static_cast<uint32_t>(target_address),
                       static_cast<uint32_t>(host_address));
                        */

                  return host_address;
                }
              }
            }
          }
        }
      }
    }
  }
  auto fn = thread_state->processor()->ResolveFunction(
      static_cast<uint32_t>(target_address));
  assert_not_null(fn);
  auto x64_fn = static_cast<X64Function*>(fn);
  uint64_t addr = reinterpret_cast<uint64_t>(x64_fn->machine_code());

  return addr;
}

void X64Emitter::Call(const hir::Instr* instr, GuestFunction* function) {
  assert_not_null(function);
  ForgetMxcsrMode();
  auto fn = static_cast<X64Function*>(function);
  // Resolve address to the function to call and store in rax.

  if (fn->machine_code()) {
    if (!(instr->flags & hir::CALL_TAIL)) {
      mov(rcx, qword[rsp + StackLayout::GUEST_CALL_RET_ADDR]);

      call((void*)fn->machine_code());
      synchronize_stack_on_next_instruction_ = true;
    } else {
      // tail call
      EmitTraceUserCallReturn();
      EmitProfilerEpilogue();
      // Pass the callers return address over.
      mov(rcx, qword[rsp + StackLayout::GUEST_RET_ADDR]);

      add(rsp, static_cast<uint32_t>(stack_size()));
      PopStackpoint();
      jmp((void*)fn->machine_code(), T_NEAR);
    }

    return;
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
    EmitProfilerEpilogue();
    // Pass the callers return address over.
    mov(rcx, qword[rsp + StackLayout::GUEST_RET_ADDR]);

    add(rsp, static_cast<uint32_t>(stack_size()));
    PopStackpoint();
    jmp(rax);
  } else {
    // Return address is from the previous SET_RETURN_ADDRESS.
    mov(rcx, qword[rsp + StackLayout::GUEST_CALL_RET_ADDR]);

    call(rax);
    synchronize_stack_on_next_instruction_ = true;
  }
}

void X64Emitter::CallIndirect(const hir::Instr* instr,
                              const Xbyak::Reg64& reg) {
  ForgetMxcsrMode();
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
    EmitProfilerEpilogue();
    // Pass the callers return address over.
    mov(rcx, qword[rsp + StackLayout::GUEST_RET_ADDR]);

    add(rsp, static_cast<uint32_t>(stack_size()));
    PopStackpoint();
    jmp(rax);
  } else {
    // Return address is from the previous SET_RETURN_ADDRESS.
    mov(rcx, qword[rsp + StackLayout::GUEST_CALL_RET_ADDR]);

    call(rax);
    synchronize_stack_on_next_instruction_ = true;
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
void X64Emitter::CallExtern(const hir::Instr* instr, const Function* function) {
  ForgetMxcsrMode();
  bool undefined = true;
  if (function->behavior() == Function::Behavior::kBuiltin) {
    auto builtin_function = static_cast<const BuiltinFunction*>(function);
    if (builtin_function->handler()) {
      undefined = false;
      // rcx = target function
      // rdx = arg0
      // r8  = arg1
      // r9  = arg2
      mov(rcx, reinterpret_cast<uint64_t>(builtin_function->handler()));
      mov(rdx, reinterpret_cast<uint64_t>(builtin_function->arg0()));
      mov(r8, reinterpret_cast<uint64_t>(builtin_function->arg1()));
      call(backend()->guest_to_host_thunk());
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
      mov(rcx, reinterpret_cast<uint64_t>(extern_function->extern_handler()));
      mov(rdx,
          qword[GetContextReg() + offsetof(ppc::PPCContext, kernel_state)]);
      call(backend()->guest_to_host_thunk());
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
  mov(rcx, reinterpret_cast<uint64_t>(fn));
  call(backend()->guest_to_host_thunk());
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
Xbyak::Reg64 X64Emitter::GetContextReg() const { return rsi; }
Xbyak::Reg64 X64Emitter::GetMembaseReg() const { return rdi; }

void X64Emitter::ReloadMembase() {
  mov(GetMembaseReg(),
      qword[GetContextReg() +
            offsetof(ppc::PPCContext, virtual_membase)]);  // membase
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
/*
    WARNING: do not use any regs here, addr is often produced by
   ComputeAddressOffset, which may use rax/rdx/rcx in its addr expression
*/
void X64Emitter::MovMem64(const Xbyak::RegExp& addr, uint64_t v) {
  uint32_t lowpart = static_cast<uint32_t>(v);
  uint32_t highpart = static_cast<uint32_t>(v >> 32);
  // check whether the constant coincidentally collides with our membase
  if (v == (uintptr_t)processor()->memory()->virtual_membase()) {
    mov(qword[addr], GetMembaseReg());
  } else if ((v & ~0x7FFFFFFF) == 0) {
    // Fits under 31 bits, so just load using normal mov.

    mov(qword[addr], v);
  } else if ((v & ~0x7FFFFFFF) == ~0x7FFFFFFF) {
    // Negative number that fits in 32bits.
    mov(qword[addr], v);
  } else if (!highpart) {
    // All high bits are zero. It'd be nice if we had a way to load a 32bit
    // immediate without sign extending!
    // TODO(benvanik): this is super common, find a better way.
    if (lowpart == 0 && CanUseMembaseLow32As0()) {
      mov(dword[addr], GetMembaseReg().cvt32());
    } else {
      mov(dword[addr], static_cast<uint32_t>(v));
    }
    if (CanUseMembaseLow32As0()) {
      mov(dword[addr + 4], GetMembaseReg().cvt32());
    } else {
      mov(dword[addr + 4], 0);
    }
  } else {
    // 64bit number that needs double movs.

    if (lowpart == 0 && CanUseMembaseLow32As0()) {
      mov(dword[addr], GetMembaseReg().cvt32());
    } else {
      mov(dword[addr], lowpart);
    }
    if (highpart == 0 && CanUseMembaseLow32As0()) {
      mov(dword[addr + 4], GetMembaseReg().cvt32());
    } else {
      mov(dword[addr + 4], highpart);
    }
  }
}
static inline vec128_t v128_setr_bytes(unsigned char v0, unsigned char v1,
                                       unsigned char v2, unsigned char v3,
                                       unsigned char v4, unsigned char v5,
                                       unsigned char v6, unsigned char v7,
                                       unsigned char v8, unsigned char v9,
                                       unsigned char v10, unsigned char v11,
                                       unsigned char v12, unsigned char v13,
                                       unsigned char v14, unsigned char v15) {
  vec128_t result;

  result.u8[0] = v0;
  result.u8[1] = v1;
  result.u8[2] = v2;
  result.u8[3] = v3;
  result.u8[4] = v4;
  result.u8[5] = v5;
  result.u8[6] = v6;
  result.u8[7] = v7;
  result.u8[8] = v8;
  result.u8[9] = v9;
  result.u8[10] = v10;
  result.u8[11] = v11;
  result.u8[12] = v12;
  result.u8[13] = v13;
  result.u8[14] = v14;

  result.u8[15] = v15;
  return result;
}

static const vec128_t xmm_consts[] = {
    /* XMMZero                */ vec128f(0.0f),
    /* XMMByteSwapMask        */
    vec128i(0x00010203u, 0x04050607u, 0x08090A0Bu, 0x0C0D0E0Fu),
    /* XMMOne                 */ vec128f(1.0f),
    /* XMMOnePD               */ vec128d(1.0),
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
    /* XMMPackULONG_4202020_MinUnpacked */
    vec128i(0x40380001u, 0x40380001u, 0x40380001u, 0x40400000u),
    /* XMMPackULONG_4202020_MaxUnpacked */
    vec128i(0x4047FFFFu, 0x4047FFFFu, 0x4047FFFFu, 0x4040000Fu),
    /* XMMPackULONG_4202020_MaskUnpacked */
    vec128i(0xFFFFFu, 0xFFFFFu, 0xFFFFFu, 0xFu),
    /* XMMPackULONG_4202020_PermuteXZ */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0x0A0908FFu, 0xFF020100u),
    /* XMMPackULONG_4202020_PermuteYW */
    vec128i(0xFFFFFFFFu, 0xFFFFFFFFu, 0x0CFFFF06u, 0x0504FFFFu),
    /* XMMUnpackULONG_4202020_Permute */
    vec128i(0xFF0E0D0Cu, 0xFF0B0A09u, 0xFF080F0Eu, 0xFFFFFF0Bu),
    /* XMMUnpackULONG_4202020_Overflow */ vec128i(0x40380000u),
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
    /* XMMQNaN                */ vec128i(0x7FC00000u),
    /* XMMInt127              */ vec128i(0x7Fu),
    /* XMM2To32               */ vec128f(0x1.0p32f),
    /* XMMFloatInf */ vec128i(0x7f800000),

    /* XMMIntsToBytes*/
    v128_setr_bytes(0, 4, 8, 12, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
                    0x80, 0x80, 0x80, 0x80),
    /*XMMShortsToBytes*/
    v128_setr_bytes(0, 2, 4, 6, 8, 10, 12, 14, 0x80, 0x80, 0x80, 0x80, 0x80,
                    0x80, 0x80, 0x80),
    /*XMMLVSLTableBase*/
    vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
    /*XMMLVSRTableBase*/
    vec128b(16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31),
    /* XMMSingleDenormalMask */
    vec128i(0x7f800000),
    /* XMMThreeFloatMask */
    vec128i(~0U, ~0U, ~0U, 0U),
    /*
        XMMF16UnpackLCPI2
        */

    vec128i(0x38000000),
    /*
                XMMF16UnpackLCPI3
        */
    vec128q(0x7fe000007fe000ULL),

    /* XMMF16PackLCPI0*/
    vec128i(0x8000000),
    /*XMMF16PackLCPI2*/
    vec128i(0x47ffe000),
    /*XMMF16PackLCPI3*/
    vec128i(0xc7800000),
    /*XMMF16PackLCPI4
     */
    vec128i(0xf7fdfff),
    /*XMMF16PackLCPI5*/
    vec128i(0x7fff),
    /*
    XMMF16PackLCPI6
    */
    vec128i(0x8000),
    /* XMMXOPByteShiftMask,*/
    vec128b(7),
    /*XMMXOPWordShiftMask*/
    vec128s(15),
    /*XMMXOPDwordShiftMask*/
    vec128i(31),
    /*XMMLVLShuffle*/
    v128_setr_bytes(3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12),
    /*XMMLVRCmp16*/
    vec128b(16),
    /*XMMSTVLShuffle*/
    v128_setr_bytes(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
    /* XMMSTVRSwapMask*/
    vec128b((uint8_t)0x83), /*XMMVSRShlByteshuf*/
    v128_setr_bytes(13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3, 0x80),
    // XMMVSRMask
    vec128b(1)};

void* X64Emitter::FindByteConstantOffset(unsigned bytevalue) {
  for (auto& vec : xmm_consts) {
    for (auto& u8 : vec.u8) {
      if (u8 == bytevalue) {
        return reinterpret_cast<void*>(backend_->emitter_data() +
                                       (&u8 - &xmm_consts[0].u8[0]));
      }
    }
  }
  return nullptr;
}
void* X64Emitter::FindWordConstantOffset(unsigned wordvalue) {
  for (auto& vec : xmm_consts) {
    for (auto& u16 : vec.u16) {
      if (u16 == wordvalue) {
        return reinterpret_cast<void*>(backend_->emitter_data() +
                                       ((&u16 - &xmm_consts[0].u16[0]) * 2));
      }
    }
  }
  return nullptr;
}
void* X64Emitter::FindDwordConstantOffset(unsigned dwordvalue) {
  for (auto& vec : xmm_consts) {
    for (auto& u32 : vec.u32) {
      if (u32 == dwordvalue) {
        return reinterpret_cast<void*>(backend_->emitter_data() +
                                       ((&u32 - &xmm_consts[0].u32[0]) * 4));
      }
    }
  }
  return nullptr;
}
void* X64Emitter::FindQwordConstantOffset(uint64_t qwordvalue) {
  for (auto& vec : xmm_consts) {
    for (auto& u64 : vec.u64) {
      if (u64 == qwordvalue) {
        return reinterpret_cast<void*>(backend_->emitter_data() +
                                       ((&u64 - &xmm_consts[0].u64[0]) * 8));
      }
    }
  }
  return nullptr;
}
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
// Implies possible StashXmm(0, ...)!
void X64Emitter::LoadConstantXmm(Xbyak::Xmm dest, const vec128_t& v) {
  // https://www.agner.org/optimize/optimizing_assembly.pdf
  // 13.4 Generating constants
  if (!v.low && !v.high) {
    // 0000...
    vpxor(dest, dest);
  } else if (v.low == ~uint64_t(0) && v.high == ~uint64_t(0)) {
    // 1111...
    vpcmpeqb(dest, dest);
  } else {
    for (size_t i = 0; i < (kConstDataSize / sizeof(vec128_t)); ++i) {
      if (xmm_consts[i] == v) {
        vmovapd(dest, GetXmmConstPtr((XmmConst)i));
        return;
      }
    }
    if (IsFeatureEnabled(kX64EmitAVX2)) {
      bool all_equal_bytes = true;

      unsigned firstbyte = v.u8[0];
      for (unsigned i = 1; i < 16; ++i) {
        if (v.u8[i] != firstbyte) {
          all_equal_bytes = false;
          break;
        }
      }

      if (all_equal_bytes) {
        void* bval = FindByteConstantOffset(firstbyte);

        if (bval) {
          vpbroadcastb(dest, byte[bval]);
          return;
        }
        // didnt find existing mem with the value
        mov(byte[rsp + kStashOffset], firstbyte);
        vpbroadcastb(dest, byte[rsp + kStashOffset]);
        return;
      }

      bool all_equal_words = true;
      unsigned firstword = v.u16[0];
      for (unsigned i = 1; i < 8; ++i) {
        if (v.u16[i] != firstword) {
          all_equal_words = false;
          break;
        }
      }
      if (all_equal_words) {
        void* wval = FindWordConstantOffset(firstword);
        if (wval) {
          vpbroadcastw(dest, word[wval]);
          return;
        }
        // didnt find existing mem with the value
        mov(word[rsp + kStashOffset], firstword);
        vpbroadcastw(dest, word[rsp + kStashOffset]);
        return;
      }

      bool all_equal_dwords = true;
      unsigned firstdword = v.u32[0];
      for (unsigned i = 1; i < 4; ++i) {
        if (v.u32[i] != firstdword) {
          all_equal_dwords = false;
          break;
        }
      }
      if (all_equal_dwords) {
        void* dwval = FindDwordConstantOffset(firstdword);
        if (dwval) {
          vpbroadcastd(dest, dword[dwval]);
          return;
        }
        mov(dword[rsp + kStashOffset], firstdword);
        vpbroadcastd(dest, dword[rsp + kStashOffset]);
        return;
      }

      bool all_equal_qwords = v.low == v.high;

      if (all_equal_qwords) {
        void* qwval = FindQwordConstantOffset(v.low);
        if (qwval) {
          vpbroadcastq(dest, qword[qwval]);
          return;
        }
        MovMem64(rsp + kStashOffset, v.low);
        vpbroadcastq(dest, qword[rsp + kStashOffset]);
        return;
      }
    }

    for (auto& vec : xmm_consts) {
      if (vec.low == v.low && vec.high == v.high) {
        vmovdqa(dest,
                ptr[reinterpret_cast<void*>(backend_->emitter_data() +
                                            ((&vec - &xmm_consts[0]) * 16))]);
        return;
      }
    }

    if (v.high == 0 && v.low == ~0ULL) {
      vpcmpeqb(dest, dest);
      movq(dest, dest);
      return;
    }
    if (v.high == 0) {
      if ((v.low & 0xFFFFFFFF) == v.low) {
        mov(dword[rsp + kStashOffset], static_cast<unsigned>(v.low));
        movd(dest, dword[rsp + kStashOffset]);
        return;
      }
      MovMem64(rsp + kStashOffset, v.low);
      movq(dest, qword[rsp + kStashOffset]);
      return;
    }

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
  if (!x.i) {
    // +0.0f (but not -0.0f because it may be used to flip the sign via xor).
    vxorps(dest, dest);
  } else if (x.i == ~uint32_t(0)) {
    // 1111...
    vcmpeqss(dest, dest);
  } else {
    unsigned raw_bits = *reinterpret_cast<unsigned*>(&v);

    for (size_t i = 0; i < (kConstDataSize / sizeof(vec128_t)); ++i) {
      if (xmm_consts[i].u32[0] == raw_bits) {
        vmovss(dest, GetXmmConstPtr((XmmConst)i));
        return;
      }
    }
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
  if (!x.i) {
    // +0.0 (but not -0.0 because it may be used to flip the sign via xor).
    vxorpd(dest, dest);
  } else if (x.i == ~uint64_t(0)) {
    // 1111...
    vcmpeqpd(dest, dest);
  } else {
    uint64_t raw_bits = *reinterpret_cast<uint64_t*>(&v);

    for (size_t i = 0; i < (kConstDataSize / sizeof(vec128_t)); ++i) {
      if (xmm_consts[i].u64[0] == raw_bits) {
        vmovsd(dest, GetXmmConstPtr((XmmConst)i));
        return;
      }
    }
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

Xbyak::Address X64Emitter::StashConstantXmm(int index, float v) {
  union {
    float f;
    uint32_t i;
  } x = {v};
  auto addr = rsp + kStashOffset + (index * 16);
  MovMem64(addr, x.i);
  MovMem64(addr + 8, 0);
  return ptr[addr];
}

Xbyak::Address X64Emitter::StashConstantXmm(int index, double v) {
  union {
    double d;
    uint64_t i;
  } x = {v};
  auto addr = rsp + kStashOffset + (index * 16);
  MovMem64(addr, x.i);
  MovMem64(addr + 8, 0);
  return ptr[addr];
}

Xbyak::Address X64Emitter::StashConstantXmm(int index, const vec128_t& v) {
  auto addr = rsp + kStashOffset + (index * 16);
  MovMem64(addr, v.low);
  MovMem64(addr + 8, v.high);
  return ptr[addr];
}
static bool IsVectorCompare(const Instr* i) {
  hir::Opcode op = i->opcode->num;
  return op >= hir::OPCODE_VECTOR_COMPARE_EQ &&
         op <= hir::OPCODE_VECTOR_COMPARE_UGE;
}

static bool IsFlaggedVectorOp(const Instr* i) {
  if (IsVectorCompare(i)) {
    return true;
  }
  hir::Opcode op = i->opcode->num;
  using namespace hir;
  switch (op) {
    case OPCODE_VECTOR_SUB:
    case OPCODE_VECTOR_ADD:
    case OPCODE_SWIZZLE:
      return true;
  }
  return false;
}

static SimdDomain GetDomainForFlaggedVectorOp(const hir::Instr* df) {
  switch (df->flags) {  // check what datatype we compared as
    case hir::INT16_TYPE:
    case hir::INT32_TYPE:
    case hir::INT8_TYPE:
    case hir::INT64_TYPE:
      return SimdDomain::INTEGER;
    case hir::FLOAT32_TYPE:
    case hir::FLOAT64_TYPE:  // pretty sure float64 doesnt occur with vectors.
                             // here for completeness
      return SimdDomain::FLOATING;
    default:
      return SimdDomain::DONTCARE;
  }
  return SimdDomain::DONTCARE;
}
// this list is incomplete
static bool IsDefiniteIntegerDomainOpcode(hir::Opcode opc) {
  using namespace hir;
  switch (opc) {
    case OPCODE_LOAD_VECTOR_SHL:
    case OPCODE_LOAD_VECTOR_SHR:
    case OPCODE_VECTOR_CONVERT_F2I:
    case OPCODE_VECTOR_MIN:  // there apparently is no FLOAT32_TYPE for min/maxs
                             // flags
    case OPCODE_VECTOR_MAX:
    case OPCODE_VECTOR_SHL:
    case OPCODE_VECTOR_SHR:
    case OPCODE_VECTOR_SHA:
    case OPCODE_VECTOR_ROTATE_LEFT:
    case OPCODE_VECTOR_AVERAGE:  // apparently no float32 type for this
    case OPCODE_EXTRACT:
    case OPCODE_INSERT:  // apparently no f32 type for these two
      return true;
  }
  return false;
}
static bool IsDefiniteFloatingDomainOpcode(hir::Opcode opc) {
  using namespace hir;
  switch (opc) {
    case OPCODE_VECTOR_CONVERT_I2F:
    case OPCODE_VECTOR_DENORMFLUSH:
    case OPCODE_DOT_PRODUCT_3:
    case OPCODE_DOT_PRODUCT_4:
    case OPCODE_LOG2:
    case OPCODE_POW2:
    case OPCODE_RECIP:
    case OPCODE_ROUND:
    case OPCODE_SQRT:
    case OPCODE_MUL:
    case OPCODE_MUL_SUB:
    case OPCODE_MUL_ADD:
    case OPCODE_ABS:
      return true;
  }
  return false;
}

SimdDomain X64Emitter::DeduceSimdDomain(const hir::Value* for_value) {
  hir::Instr* df = for_value->def;
  if (!df) {
    // todo: visit uses to figure out domain
    return SimdDomain::DONTCARE;

  } else {
    SimdDomain result = SimdDomain::DONTCARE;

    if (IsFlaggedVectorOp(df)) {
      result = GetDomainForFlaggedVectorOp(df);
    } else if (IsDefiniteIntegerDomainOpcode(df->opcode->num)) {
      result = SimdDomain::INTEGER;
    } else if (IsDefiniteFloatingDomainOpcode(df->opcode->num)) {
      result = SimdDomain::FLOATING;
    }

    // todo: check if still dontcare, if so, visit uses of the value to figure
    // it out
    return result;
  }

  return SimdDomain::DONTCARE;
}
Xbyak::Address X64Emitter::GetBackendCtxPtr(int offset_in_x64backendctx) const {
  /*
    index context ptr negatively to get to backend ctx field
  */
  ptrdiff_t delta = (-static_cast<ptrdiff_t>(sizeof(X64BackendContext))) +
                    offset_in_x64backendctx;
  return ptr[GetContextReg() + static_cast<int>(delta)];
}
Xbyak::Label& X64Emitter::AddToTail(TailEmitCallback callback,
                                    uint32_t alignment) {
  TailEmitter emitter{};
  emitter.func = std::move(callback);
  emitter.alignment = alignment;
  tail_code_.push_back(std::move(emitter));
  return tail_code_.back().label;
}
Xbyak::Label& X64Emitter::NewCachedLabel() {
  Xbyak::Label* tmp = new Xbyak::Label;
  label_cache_.push_back(tmp);
  return *tmp;
}

template <bool switching_to_fpu>
static void ChangeMxcsrModeDynamicHelper(X64Emitter& e) {
  auto flags = e.GetBackendFlagsPtr();
  if (switching_to_fpu) {
    e.btr(flags, 0);  // bit 0 set to 0 = is fpu mode
  } else {
    e.bts(flags, 0);  // bit 0 set to 1 = is vmx mode
  }
  Xbyak::Label& come_back = e.NewCachedLabel();

  Xbyak::Label& reload_bailout =
      e.AddToTail([&come_back](X64Emitter& e, Xbyak::Label& thislabel) {
        e.L(thislabel);
        if (switching_to_fpu) {
          e.LoadFpuMxcsrDirect();
        } else {
          e.LoadVmxMxcsrDirect();
        }
        e.jmp(come_back, X64Emitter::T_NEAR);
      });
  if (switching_to_fpu) {
    e.jc(reload_bailout,
         X64Emitter::T_NEAR);  // if carry flag was set, we were VMX mxcsr mode.
  } else {
    e.jnc(
        reload_bailout,
        X64Emitter::T_NEAR);  // if carry flag was set, we were VMX mxcsr mode.
  }
  e.L(come_back);
}

bool X64Emitter::ChangeMxcsrMode(MXCSRMode new_mode, bool already_set) {
  if (cvars::enable_incorrect_roundingmode_behavior) {
    return false;  // no MXCSR mode handling!
  }
  if (new_mode == mxcsr_mode_) {
    return false;
  }
  assert_true(new_mode != MXCSRMode::Unknown);

  if (mxcsr_mode_ == MXCSRMode::Unknown) {
    // check the mode dynamically
    mxcsr_mode_ = new_mode;
    if (!already_set) {
      if (new_mode == MXCSRMode::Fpu) {
        ChangeMxcsrModeDynamicHelper<true>(*this);
      } else if (new_mode == MXCSRMode::Vmx) {
        ChangeMxcsrModeDynamicHelper<false>(*this);
      } else {
        assert_unhandled_case(new_mode);
      }
    } else {  // even if already set, we still need to update flags to reflect
              // our mode
      if (new_mode == MXCSRMode::Fpu) {
        btr(GetBackendFlagsPtr(), 0);
      } else if (new_mode == MXCSRMode::Vmx) {
        bts(GetBackendFlagsPtr(), 0);
      } else {
        assert_unhandled_case(new_mode);
      }
    }
  } else {
    mxcsr_mode_ = new_mode;
    if (!already_set) {
      if (new_mode == MXCSRMode::Fpu) {
        LoadFpuMxcsrDirect();
        btr(GetBackendFlagsPtr(), 0);
        return true;
      } else if (new_mode == MXCSRMode::Vmx) {
        LoadVmxMxcsrDirect();
        bts(GetBackendFlagsPtr(), 0);
        return true;
      } else {
        assert_unhandled_case(new_mode);
      }
    }
  }
  return false;
}
void X64Emitter::LoadFpuMxcsrDirect() {
  vldmxcsr(GetBackendCtxPtr(offsetof(X64BackendContext, mxcsr_fpu)));
}
void X64Emitter::LoadVmxMxcsrDirect() {
  vldmxcsr(GetBackendCtxPtr(offsetof(X64BackendContext, mxcsr_vmx)));
}
Xbyak::Address X64Emitter::GetBackendFlagsPtr() const {
  Xbyak::Address pt = GetBackendCtxPtr(offsetof(X64BackendContext, flags));
  pt.setBit(32);
  return pt;
}

void X64Emitter::HandleStackpointOverflowError(ppc::PPCContext* context) {
  if (debugging::IsDebuggerAttached()) {
    debugging::Break();
  }
  // context->lr
  // todo: show lr in message?
  xe::FatalError(
      "Overflowed stackpoints! Please report this error for this title to "
      "Xenia developers.");
}

void X64Emitter::PushStackpoint() {
  if (!cvars::enable_host_guest_stack_synchronization) {
    return;
  }
  // push the current host and guest stack pointers
  // this is done before a stack frame is set up or any guest instructions are
  // executed this code is probably the most intrusive part of the stackpoint
  mov(rbx, GetBackendCtxPtr(offsetof(X64BackendContext, stackpoints)));
  mov(eax,
      GetBackendCtxPtr(offsetof(X64BackendContext, current_stackpoint_depth)));

  mov(r8, qword[GetContextReg() + offsetof(ppc::PPCContext, r[1])]);

  imul(r9d, eax, sizeof(X64BackendStackpoint));
  add(rbx, r9);

  mov(qword[rbx + offsetof(X64BackendStackpoint, host_stack_)], rsp);
  mov(dword[rbx + offsetof(X64BackendStackpoint, guest_stack_)], r8d);
  mov(r8d, qword[GetContextReg() + offsetof(ppc::PPCContext, lr)]);
  mov(dword[rbx + offsetof(X64BackendStackpoint, guest_return_address_)], r8d);

  if (IsFeatureEnabled(kX64FlagsIndependentVars)) {
    inc(eax);
  } else {
    add(eax, 1);
  }

  mov(GetBackendCtxPtr(offsetof(X64BackendContext, current_stackpoint_depth)),
      eax);

  cmp(eax, (uint32_t)cvars::max_stackpoints);

  Xbyak::Label& overflowed_stackpoints =
      AddToTail([](X64Emitter& e, Xbyak::Label& our_tail_label) {
        e.L(our_tail_label);
        // we never subtracted anything from rsp, so our stack is misaligned and
        // will fault in guesttohostthunk
        // e.sub(e.rsp, 8);
        e.push(e.rax);  // easier realign, 1 byte opcode vs 4 bytes for sub

        e.CallNativeSafe((void*)X64Emitter::HandleStackpointOverflowError);
      });
  jge(overflowed_stackpoints, T_NEAR);
}
void X64Emitter::PopStackpoint() {
  if (!cvars::enable_host_guest_stack_synchronization) {
    return;
  }
  // todo: maybe verify that rsp and r1 == the stackpoint?
  Xbyak::Address stackpoint_pos_pointer =
      GetBackendCtxPtr(offsetof(X64BackendContext, current_stackpoint_depth));
  stackpoint_pos_pointer.setBit(32);
  dec(stackpoint_pos_pointer);
}

void X64Emitter::EnsureSynchronizedGuestAndHostStack() {
  if (!cvars::enable_host_guest_stack_synchronization) {
    return;
  }
  // chrispy: keeping this old slower test here in case in the future changes
  // need to be made
  // that result in the stack not being 8 byte misaligned on context reentry

  Xbyak::Label& return_from_sync = this->NewCachedLabel();

  // if we got here somehow from setjmp or the like we ought to have a
  // misaligned stack right now! this provides us with a very fast pretest for
  // this condition
  test(esp, 15);

  Xbyak::Label& sync_label = this->AddToTail(
      [&return_from_sync](X64Emitter& e, Xbyak::Label& our_tail_label) {
        e.L(our_tail_label);

        uint32_t stack32 = static_cast<uint32_t>(e.stack_size());
        auto backend = e.backend();
        if (stack32 < 256) {
          e.call(backend->synchronize_guest_and_host_stack_helper_for_size(1));
          e.db(stack32);

        } else if (stack32 < 65536) {
          e.call(backend->synchronize_guest_and_host_stack_helper_for_size(2));
          e.dw(stack32);
        } else {
          // ought to be impossible, a host stack bigger than 65536??
          e.call(backend->synchronize_guest_and_host_stack_helper_for_size(4));
          e.dd(stack32);
        }
        e.jmp(return_from_sync, T_NEAR);
      });

  jnz(sync_label, T_NEAR);

  L(return_from_sync);
}
}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
