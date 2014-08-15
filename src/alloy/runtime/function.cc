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
#include <xdb/protocol.h>

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

  uint64_t trace_base = thread_state->memory()->trace_base();
  if (symbol_info_->behavior() == FunctionInfo::BEHAVIOR_EXTERN) {
    auto handler = symbol_info_->extern_handler();

    if (trace_base && true) {
      auto ev = xdb::protocol::KernelCallEvent::Append(trace_base);
      ev->type = xdb::protocol::EventType::KERNEL_CALL;
      ev->thread_id = thread_state->thread_id();
      ev->module_id = 0;
      ev->ordinal = 0;
    }

    if (handler) {
      handler(thread_state->raw_context(), symbol_info_->extern_arg0(),
              symbol_info_->extern_arg1());
    } else {
      XELOGW("undefined extern call to %.8llX %s", symbol_info_->address(),
             symbol_info_->name().c_str());
      result = 1;
    }

    if (trace_base && true) {
      auto ev = xdb::protocol::KernelCallReturnEvent::Append(trace_base);
      ev->type = xdb::protocol::EventType::KERNEL_CALL_RETURN;
      ev->thread_id = thread_state->thread_id();
    }
  } else {
    if (trace_base && true) {
      auto ev = xdb::protocol::UserCallEvent::Append(trace_base);
      ev->type = xdb::protocol::EventType::USER_CALL;
      ev->call_type = 0; // ?
      ev->thread_id = thread_state->thread_id();
      ev->address = static_cast<uint32_t>(symbol_info_->address());
    }

    CallImpl(thread_state, return_address);

    if (trace_base && true) {
      auto ev = xdb::protocol::UserCallReturnEvent::Append(trace_base);
      ev->type = xdb::protocol::EventType::USER_CALL_RETURN;
      ev->thread_id = thread_state->thread_id();
    }
  }

  if (original_thread_state != thread_state) {
    ThreadState::Bind(original_thread_state);
  }
  return result;
}

}  // namespace runtime
}  // namespace alloy
