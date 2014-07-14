/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_RUNTIME_FUNCTION_H_
#define ALLOY_RUNTIME_FUNCTION_H_

#include <memory>
#include <mutex>
#include <vector>

#include <alloy/core.h>
#include <alloy/runtime/debug_info.h>

namespace alloy {
namespace runtime {

class Breakpoint;
class FunctionInfo;
class ThreadState;

class Function {
 public:
  Function(FunctionInfo* symbol_info);
  virtual ~Function();

  uint64_t address() const { return address_; }
  FunctionInfo* symbol_info() const { return symbol_info_; }

  DebugInfo* debug_info() const { return debug_info_.get(); }
  void set_debug_info(std::unique_ptr<DebugInfo> debug_info) {
    debug_info_ = std::move(debug_info);
  }

  int AddBreakpoint(Breakpoint* breakpoint);
  int RemoveBreakpoint(Breakpoint* breakpoint);

  int Call(ThreadState* thread_state, uint64_t return_address);

 protected:
  Breakpoint* FindBreakpoint(uint64_t address);
  virtual int AddBreakpointImpl(Breakpoint* breakpoint) { return 0; }
  virtual int RemoveBreakpointImpl(Breakpoint* breakpoint) { return 0; }
  virtual int CallImpl(ThreadState* thread_state, uint64_t return_address) = 0;

 protected:
  uint64_t address_;
  FunctionInfo* symbol_info_;
  std::unique_ptr<DebugInfo> debug_info_;

  // TODO(benvanik): move elsewhere? DebugData?
  std::mutex lock_;
  std::vector<Breakpoint*> breakpoints_;
};

}  // namespace runtime
}  // namespace alloy

#endif  // ALLOY_RUNTIME_FUNCTION_H_
