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

namespace alloy {
namespace runtime {

Function::Function(FunctionInfo* symbol_info)
    : address_(symbol_info->address()), symbol_info_(symbol_info) {}

Function::~Function() = default;

int Function::AddBreakpoint(Breakpoint* breakpoint) {
  std::lock_guard<std::mutex> guard(lock_);
  bool found = false;
  for (auto other : breakpoints_) {
    if (other == breakpoint) {
      found = true;
      break;
    }
  }
  if (!found) {
    breakpoints_.push_back(breakpoint);
    AddBreakpointImpl(breakpoint);
  }
  return found ? 1 : 0;
}

int Function::RemoveBreakpoint(Breakpoint* breakpoint) {
  std::lock_guard<std::mutex> guard(lock_);
  bool found = false;
  for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
    if (*it == breakpoint) {
      breakpoints_.erase(it);
      RemoveBreakpointImpl(breakpoint);
      found = true;
      break;
    }
  }
  return found ? 0 : 1;
}

Breakpoint* Function::FindBreakpoint(uint64_t address) {
  std::lock_guard<std::mutex> guard(lock_);
  Breakpoint* result = nullptr;
  for (auto breakpoint : breakpoints_) {
    if (breakpoint->address() == address) {
      result = breakpoint;
      break;
    }
  }
  return result;
}

int Function::Call(ThreadState* thread_state, uint64_t return_address) {
  SCOPE_profile_cpu_f("alloy");

  ThreadState* original_thread_state = ThreadState::Get();
  if (original_thread_state != thread_state) {
    ThreadState::Bind(thread_state);
  }

  int result = 0;

  if (symbol_info_->behavior() == FunctionInfo::BEHAVIOR_EXTERN) {
    auto handler = symbol_info_->extern_handler();
    if (handler) {
      handler(thread_state->raw_context(), symbol_info_->extern_arg0(),
              symbol_info_->extern_arg1());
    } else {
      XELOGW("undefined extern call to %.8llX %s", symbol_info_->address(),
             symbol_info_->name().c_str());
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

}  // namespace runtime
}  // namespace alloy
