/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/instrument.h"

#include "xenia/memory.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/processor.h"

namespace xe {
namespace cpu {

Instrument::Instrument(Processor* processor)
    : processor_(processor),
      memory_(processor->memory()),
      is_attached_(false) {}

Instrument::~Instrument() {
  if (is_attached_) {
    Detach();
  }
}

bool Instrument::Attach() {
  if (is_attached_) {
    return false;
  }
  // runtime->AttachInstrument(this);
  is_attached_ = true;
  return true;
}

bool Instrument::Detach() {
  if (!is_attached_) {
    return false;
  }
  is_attached_ = false;
  // runtime->DetachInstrument(this);
  return true;
}

FunctionInstrument::FunctionInstrument(Processor* processor, Function* function)
    : Instrument(processor), target_(function) {}

bool FunctionInstrument::Attach() {
  if (!Instrument::Attach()) {
    return false;
  }

  // Function impl attach:
  // - add instrument to list
  // JIT: transition to instrumented state
  //      - rewrite enter/exit to jump to instrumentation thunk
  //      - some sort of locking required?

  return true;
}

bool FunctionInstrument::Detach() {
  if (!Instrument::Detach()) {
    return false;
  }

  //

  return true;
}

void FunctionInstrument::Enter(ThreadState* thread_state) {
  // ? Placement new? How to get instance type?
}

void FunctionInstrument::Exit(ThreadState* thread_state) {
  //
}

MemoryInstrument::MemoryInstrument(Processor* processor, uint32_t address,
                                   uint32_t end_address)
    : Instrument(processor), address_(address), end_address_(end_address) {}

bool MemoryInstrument::Attach() {
  if (!Instrument::Attach()) {
    return false;
  }

  // TODO(benvanik): protect memory page, catch exception
  // https://github.com/frida/frida-gum/blob/master/gum/gummemoryaccessmonitor.c

  return true;
}

bool MemoryInstrument::Detach() {
  if (!Instrument::Detach()) {
    return false;
  }

  //

  return true;
}

void MemoryInstrument::Access(ThreadState* thread_state, uint32_t address,
                              AccessType type) {
  // TODO(benvanik): get thread local instance
}

}  // namespace cpu
}  // namespace xe
