/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/debug/debugger.h"

#include <mutex>

#include "xenia/cpu/function.h"
#include "xenia/cpu/processor.h"

namespace xe {
namespace debug {

using xe::cpu::ThreadState;

Breakpoint::Breakpoint(Type type, uint32_t address)
    : type_(type), address_(address) {}

Breakpoint::~Breakpoint() = default;

Debugger::Debugger(cpu::Processor* processor) : processor_(processor) {}

Debugger::~Debugger() = default;

int Debugger::SuspendAllThreads(uint32_t timeout_ms) {
  std::lock_guard<std::mutex> guard(threads_lock_);

  int result = 0;
  for (auto thread_state : threads_) {
    if (thread_state.second->Suspend(timeout_ms)) {
      result = 1;
    }
  }
  return result;
}

int Debugger::ResumeThread(uint32_t thread_id) {
  std::lock_guard<std::mutex> guard(threads_lock_);

  auto it = threads_.find(thread_id);
  if (it == threads_.end()) {
    return 1;
  }

  // Found thread. Note that it could be deleted as soon as we unlock.
  ThreadState* thread_state = it->second;
  int result = thread_state->Resume();

  return result;
}

int Debugger::ResumeAllThreads(bool force) {
  std::lock_guard<std::mutex> guard(threads_lock_);

  int result = 0;
  for (auto thread_state : threads_) {
    if (thread_state.second->Resume(force)) {
      result = 1;
    }
  }
  return result;
}

void Debugger::ForEachThread(std::function<void(ThreadState*)> callback) {
  std::lock_guard<std::mutex> guard(threads_lock_);

  for (auto thread_state : threads_) {
    callback(thread_state.second);
  }
}

int Debugger::AddBreakpoint(Breakpoint* breakpoint) {
  // Add to breakpoints map.
  {
    std::lock_guard<std::mutex> guard(breakpoints_lock_);
    breakpoints_.insert(
        std::pair<uint32_t, Breakpoint*>(breakpoint->address(), breakpoint));
  }

  // Find all functions that contain the breakpoint address.
  auto fns = processor_->FindFunctionsWithAddress(breakpoint->address());

  // Add.
  for (auto fn : fns) {
    if (fn->AddBreakpoint(breakpoint)) {
      return 1;
    }
  }

  return 0;
}

int Debugger::RemoveBreakpoint(Breakpoint* breakpoint) {
  // Remove from breakpoint map.
  {
    std::lock_guard<std::mutex> guard(breakpoints_lock_);
    auto range = breakpoints_.equal_range(breakpoint->address());
    if (range.first == range.second) {
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
    if (!found) {
      return 1;
    }
  }

  // Find all functions that have the breakpoint set.
  auto fns = processor_->FindFunctionsWithAddress(breakpoint->address());

  // Remove.
  for (auto fn : fns) {
    fn->RemoveBreakpoint(breakpoint);
  }

  return 0;
}

void Debugger::FindBreakpoints(uint32_t address,
                               std::vector<Breakpoint*>& out_breakpoints) {
  std::lock_guard<std::mutex> guard(breakpoints_lock_);

  out_breakpoints.clear();

  auto range = breakpoints_.equal_range(address);
  if (range.first == range.second) {
    return;
  }

  for (auto it = range.first; it != range.second; ++it) {
    Breakpoint* breakpoint = it->second;
    out_breakpoints.push_back(breakpoint);
  }
}

void Debugger::OnThreadCreated(ThreadState* thread_state) {
  std::lock_guard<std::mutex> guard(threads_lock_);
  threads_[thread_state->thread_id()] = thread_state;
}

void Debugger::OnThreadDestroyed(ThreadState* thread_state) {
  std::lock_guard<std::mutex> guard(threads_lock_);
  auto it = threads_.find(thread_state->thread_id());
  if (it != threads_.end()) {
    threads_.erase(it);
  }
}

void Debugger::OnFunctionDefined(cpu::FunctionInfo* symbol_info,
                                 cpu::Function* function) {
  // Man, I'd love not to take this lock.
  std::vector<Breakpoint*> breakpoints;
  {
    std::lock_guard<std::mutex> guard(breakpoints_lock_);
    for (uint32_t address = symbol_info->address();
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
  }

  if (breakpoints.size()) {
    // Breakpoints to add!
    for (auto breakpoint : breakpoints) {
      function->AddBreakpoint(breakpoint);
    }
  }
}

void Debugger::OnBreakpointHit(ThreadState* thread_state,
                               Breakpoint* breakpoint) {
  // Suspend all threads immediately.
  SuspendAllThreads();

  // Notify listeners.
  BreakpointHitEvent e(this, thread_state, breakpoint);
  breakpoint_hit(e);

  // Note that we stay suspended.
}

}  // namespace debug
}  // namespace xe
