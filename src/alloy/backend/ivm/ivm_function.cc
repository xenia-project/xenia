/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/ivm/ivm_function.h>

#include <alloy/backend/tracing.h>
#include <alloy/runtime/runtime.h>
#include <alloy/runtime/thread_state.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::backend::ivm;
using namespace alloy::runtime;


IVMFunction::IVMFunction(FunctionInfo* symbol_info) :
    register_count_(0), intcode_count_(0), intcodes_(0),
    source_map_count_(0), source_map_(0),
    GuestFunction(symbol_info) {
}

IVMFunction::~IVMFunction() {
  xe_free(intcodes_);
  xe_free(source_map_);
}

void IVMFunction::Setup(TranslationContext& ctx) {
  register_count_ = ctx.register_count;
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
  if (!i->debug_flags ||
      breakpoint->type() == Breakpoint::TEMP_TYPE) {
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
      uint64_t breakpoint_ptr = (uint64_t)breakpoint;
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

int IVMFunction::CallImpl(ThreadState* thread_state, uint64_t return_address) {
  // Setup register file on stack.
  size_t register_file_size = register_count_ * sizeof(Register);
  Register* register_file = (Register*)alloca(register_file_size);

  Memory* memory = thread_state->memory();

  IntCodeState ics;
  ics.rf = register_file;
  ics.context = (uint8_t*)thread_state->raw_context();
  ics.membase = memory->membase();
  ics.reserve_address = memory->reserve_address();
  ics.did_carry = 0;
  ics.access_callbacks = thread_state->runtime()->access_callbacks();
  ics.thread_state = thread_state;
  ics.return_address = return_address;
  ics.call_return_address = 0;

  // TODO(benvanik): DID_CARRY -- need HIR to set a OPCODE_FLAG_SET_CARRY
  //                 or something so the fns can set an ics flag.

  uint32_t ia = 0;
  while (true) {
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
    }
  }

  return 0;
}
