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


Function::Function(Type type, uint64_t address) :
    type_(type), address_(address), debug_info_(0) {
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
  int result = CallImpl(thread_state, return_address);
  if (original_thread_state != thread_state) {
    ThreadState::Bind(original_thread_state);
  }
  return result;
}

ExternFunction::ExternFunction(
    uint64_t address, Handler handler, void* arg0, void* arg1) :
    name_(0),
    handler_(handler), arg0_(arg0), arg1_(arg1),
    Function(Function::EXTERN_FUNCTION, address) {
}

ExternFunction::~ExternFunction() {
  if (name_) {
    xe_free(name_);
  }
}

void ExternFunction::set_name(const char* name) {
  name_ = xestrdupa(name);
}

int ExternFunction::CallImpl(ThreadState* thread_state,
                             uint64_t return_address) {
  if (!handler_) {
    XELOGW("undefined extern call to %.8X %s", address(), name());
    return 0;
  }
  handler_(thread_state->raw_context(), arg0_, arg1_);
  return 0;
}

GuestFunction::GuestFunction(FunctionInfo* symbol_info) :
    symbol_info_(symbol_info),
    Function(Function::USER_FUNCTION, symbol_info->address()) {
}

GuestFunction::~GuestFunction() {
}
