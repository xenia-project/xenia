/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/runtime/function.h>

#include <alloy/runtime/debugger.h>
#include <alloy/runtime/symbol_info.h>
#include <alloy/runtime/thread_state.h>

using namespace alloy;
using namespace alloy::runtime;


Function::Function(FunctionInfo* symbol_info) :
    address_(symbol_info->address()),
    symbol_info_(symbol_info), debug_info_(0) {
  // TODO(benvanik): create on demand?
  lock_ = AllocMutex();
}

Function::~Function() {
  FreeMutex(lock_);
}

int Function::AddBreakpoint(Breakpoint* breakpoint) {
  LockMutex(lock_);
  bool found = false;
  for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
    if (*it == breakpoint) {
      found = true;
    }
  }
  if (!found) {
    breakpoints_.push_back(breakpoint);
    AddBreakpointImpl(breakpoint);
  }
  UnlockMutex(lock_);
  return found ? 1 : 0;
}

int Function::RemoveBreakpoint(Breakpoint* breakpoint) {
  LockMutex(lock_);
  bool found = false;
  for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
    if (*it == breakpoint) {
      breakpoints_.erase(it);
      RemoveBreakpointImpl(breakpoint);
      found = true;
      break;
    }
  }
  UnlockMutex(lock_);
  return found ? 0 : 1;
}

Breakpoint* Function::FindBreakpoint(uint64_t address) {
  LockMutex(lock_);
  Breakpoint* result = NULL;
  for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
    Breakpoint* breakpoint = *it;
    if (breakpoint->address() == address) {
      result = breakpoint;
      break;
    }
  }
  UnlockMutex(lock_);
  return result;
}

int Function::Call(ThreadState* thread_state, uint64_t return_address) {
  ThreadState* original_thread_state = ThreadState::Get();
  if (original_thread_state != thread_state) {
    ThreadState::Bind(thread_state);
  }

  int result = 0;
  
  if (symbol_info_->behavior() == FunctionInfo::BEHAVIOR_EXTERN) {
    auto handler = symbol_info_->extern_handler();
    if (handler) {
      handler(thread_state->raw_context(),
              symbol_info_->extern_arg0(),
              symbol_info_->extern_arg1());
    } else {
      XELOGW("undefined extern call to %.8X %s",
             symbol_info_->address(),
             symbol_info_->name());
      result = 1;
    }
  } else {
    CallImpl(thread_state, return_address);
  }

  if (original_thread_state != thread_state) {
    ThreadState::Bind(original_thread_state);
  }
  return result;
}
