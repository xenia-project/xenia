/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/runtime/debugger.h>

#include <alloy/mutex.h>
#include <alloy/runtime/runtime.h>

using namespace alloy;
using namespace alloy::runtime;


Breakpoint::Breakpoint(Type type, uint64_t address) :
    type_(type), address_(address) {
}

Breakpoint::~Breakpoint() {
}

Debugger::Debugger(Runtime* runtime) :
    runtime_(runtime) {
  breakpoints_lock_ = AllocMutex();
}

Debugger::~Debugger() {
  FreeMutex(breakpoints_lock_);
}

int Debugger::AddBreakpoint(Breakpoint* breakpoint) {
  // Add to breakpoints map.
  LockMutex(breakpoints_lock_);
  breakpoints_.insert(
      std::pair<uint64_t, Breakpoint*>(breakpoint->address(), breakpoint));
  UnlockMutex(breakpoints_lock_);

  // Find all functions that contain the breakpoint address.
  auto fns = runtime_->FindFunctionsWithAddress(breakpoint->address());

  // Add.
  for (auto it = fns.begin(); it != fns.end(); ++it) {
    Function* fn = *it;
    if (fn->AddBreakpoint(breakpoint)) {
      return 1;
    }
  }

  return 0;
}

int Debugger::RemoveBreakpoint(Breakpoint* breakpoint) {
  // Remove from breakpoint map.
  LockMutex(breakpoints_lock_);
  auto range = breakpoints_.equal_range(breakpoint->address());
  if (range.first == range.second) {
    UnlockMutex(breakpoints_lock_);
    return 1;
  }
  bool found = false;
  for (auto it = range.first; it != range.second; ++it) {
    if (it->second == breakpoint) {
      breakpoints_.erase(it);
      found = true;
      break;
    }
  }
  UnlockMutex(breakpoints_lock_);
  if (!found) {
    return 1;
  }

  // Find all functions that have the breakpoint set.
  auto fns = runtime_->FindFunctionsWithAddress(breakpoint->address());

  // Remove.
  for (auto it = fns.begin(); it != fns.end(); ++it) {
    Function* fn = *it;
    fn->RemoveBreakpoint(breakpoint);
  }

  return 0;
}

void Debugger::FindBreakpoints(
    uint64_t address, std::vector<Breakpoint*>& out_breakpoints) {
  out_breakpoints.clear();

  LockMutex(breakpoints_lock_);

  auto range = breakpoints_.equal_range(address);
  if (range.first == range.second) {
    UnlockMutex(breakpoints_lock_);
    return;
  }

  for (auto it = range.first; it != range.second; ++it) {
    Breakpoint* breakpoint = it->second;
    out_breakpoints.push_back(breakpoint);
  }

  UnlockMutex(breakpoints_lock_);
}

void Debugger::OnFunctionDefined(FunctionInfo* symbol_info,
                                 Function* function) {
  // Man, I'd love not to take this lock.
  std::vector<Breakpoint*> breakpoints;
  LockMutex(breakpoints_lock_);
  for (uint64_t address = symbol_info->address();
       address <= symbol_info->end_address(); address += 4) {
    auto range = breakpoints_.equal_range(address);
    if (range.first == range.second) {
      continue;
    }
    for (auto it = range.first; it != range.second; ++it) {
      Breakpoint* breakpoint = it->second;
      breakpoints.push_back(breakpoint);
    }
  }
  UnlockMutex(breakpoints_lock_);

  if (breakpoints.size()) {
    // Breakpoints to add!
    for (auto it = breakpoints.begin(); it != breakpoints.end(); ++it) {
      function->AddBreakpoint(*it);
    }
  }
}
