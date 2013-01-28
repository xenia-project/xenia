/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/cpu/thread_state.h>


using namespace xe;
using namespace xe::cpu;


ThreadState::ThreadState(
    Processor* processor,
    uint32_t stack_address, uint32_t stack_size) {
  stack_address_ = stack_address;
  stack_size_ = stack_size;

  xe_zero_struct(&ppc_state_, sizeof(ppc_state_));

  // Stash pointers to common structures that callbacks may need.
  ppc_state_.processor    = processor;
  ppc_state_.thread_state = this;

  // Set initial registers.
  ppc_state_.r[1] = stack_address_;
}

ThreadState::~ThreadState() {
}

xe_ppc_state_t* ThreadState::ppc_state() {
  return &ppc_state_;
}
