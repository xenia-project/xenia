/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/ivm/ivm_function.h>

#include <alloy/backend/ivm/ivm_stack.h>
#include <alloy/runtime/runtime.h>
#include <alloy/runtime/thread_state.h>

namespace alloy {
namespace backend {
namespace ivm {

using alloy::runtime::Breakpoint;
using alloy::runtime::FunctionInfo;
using alloy::runtime::ThreadState;

IVMFunction::IVMFunction(FunctionInfo* symbol_info)
    : Function(symbol_info),
      register_count_(0),
      intcode_count_(0),
      intcodes_(0),
      source_map_count_(0),
      source_map_(0) {}

IVMFunction::~IVMFunction() {
  xe_free(intcodes_);
  xe_free(source_map_);
}

void IVMFunction::Setup(TranslationContext& ctx) {
  register_count_ = ctx.register_count;
  stack_size_ = ctx.stack_size;
  intcode_count_ = ctx.intcode_count;
  intcodes_ = (IntCode*)ctx.intcode_arena->CloneContents();
  source_map_count_ = ctx.source_map_count;
  source_map_ = (SourceMapEntry*)ctx.source_map_arena->CloneContents();
}

IntCode* IVMFunction::GetIntCodeAtSourceOffset(uint64_t offset) {
  for (size_t n = 0; n < source_map_count_; n++) {
    auto entry = &source_map_[n];
    if (entry->source_offset == offset) {
      return &intcodes_[entry->intcode_index];
    }
  }
  return NULL;
}

int IVMFunction::AddBreakpointImpl(Breakpoint* breakpoint) {
  auto i = GetIntCodeAtSourceOffset(breakpoint->address());
  if (!i) {
    return 1;
  }

  // TEMP breakpoints always overwrite normal ones.
  if (!i->debug_flags || breakpoint->type() == Breakpoint::TEMP_TYPE) {
    uint64_t breakpoint_ptr = (uint64_t)breakpoint;
    i->src2_reg = (uint32_t)breakpoint_ptr;
    i->src3_reg = (uint32_t)(breakpoint_ptr >> 32);
  }

  // Increment breakpoint counter.
  ++i->debug_flags;

  return 0;
}

int IVMFunction::RemoveBreakpointImpl(Breakpoint* breakpoint) {
  auto i = GetIntCodeAtSourceOffset(breakpoint->address());
  if (!i) {
    return 1;
  }

  // Decrement breakpoint counter.
  --i->debug_flags;
  i->src2_reg = i->src3_reg = 0;

  // If there were other breakpoints, see what they were.
  if (i->debug_flags) {
    auto old_breakpoint = FindBreakpoint(breakpoint->address());
    if (old_breakpoint) {
      uint64_t breakpoint_ptr = (uint64_t)old_breakpoint;
      i->src2_reg = (uint32_t)breakpoint_ptr;
      i->src3_reg = (uint32_t)(breakpoint_ptr >> 32);
    }
  }

  return 0;
}

void IVMFunction::OnBreakpointHit(ThreadState* thread_state, IntCode* i) {
  uint64_t breakpoint_ptr = i->src2_reg | (uint64_t(i->src3_reg) << 32);
  Breakpoint* breakpoint = (Breakpoint*)breakpoint_ptr;

  // Notify debugger.
  // The debugger may choose to wait (blocking us).
  auto debugger = thread_state->runtime()->debugger();
  debugger->OnBreakpointHit(thread_state, breakpoint);
}

#undef TRACE_SOURCE_OFFSET

int IVMFunction::CallImpl(ThreadState* thread_state, uint64_t return_address) {
  // Setup register file on stack.
  auto stack = (IVMStack*)thread_state->backend_data();
  auto register_file = (Register*)stack->Alloc(register_count_);
  auto local_stack = (uint8_t*)alloca(stack_size_);

  Memory* memory = thread_state->memory();

  IntCodeState ics;
  ics.rf = register_file;
  ics.locals = local_stack;
  ics.context = (uint8_t*)thread_state->raw_context();
  ics.membase = memory->membase();
  ics.page_table = ics.membase + memory->page_table();
  ics.did_carry = 0;
  ics.did_saturate = 0;
  ics.thread_state = thread_state;
  ics.return_address = return_address;
  ics.call_return_address = 0;

// TODO(benvanik): DID_CARRY -- need HIR to set a OPCODE_FLAG_SET_CARRY
//                 or something so the fns can set an ics flag.

#ifdef TRACE_SOURCE_OFFSET
  size_t source_index = 0;
#endif

  uint32_t ia = 0;
  while (true) {
#ifdef TRACE_SOURCE_OFFSET
    uint64_t source_offset = -1;
    if (source_index < this->source_map_count_ &&
        this->source_map_[source_index].intcode_index <= ia) {
      while (source_index + 1 < this->source_map_count_ &&
             this->source_map_[source_index + 1].intcode_index <= ia) {
        source_index++;
      }
      source_offset = this->source_map_[source_index].source_offset;
    }
#endif

    IntCode* i = &intcodes_[ia];

    if (i->debug_flags) {
      OnBreakpointHit(thread_state, i);
    }

    uint32_t new_ia = i->intcode_fn(ics, i);
    if (new_ia == IA_NEXT) {
      ia++;
    } else if (new_ia == IA_RETURN) {
      break;
    } else {
      ia = new_ia;
#ifdef TRACE_SOURCE_OFFSET
      source_index = 0;
#endif
    }
  }

  stack->Free(register_count_);

  return 0;
}

}  // namespace ivm
}  // namespace backend
}  // namespace alloy
