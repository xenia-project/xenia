/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_X64_FUNCTION_H_
#define ALLOY_BACKEND_X64_X64_FUNCTION_H_

#include <alloy/core.h>
#include <alloy/runtime/function.h>
#include <alloy/runtime/symbol_info.h>

namespace alloy {
namespace backend {
namespace x64 {

class X64Function : public runtime::Function {
 public:
  X64Function(runtime::FunctionInfo* symbol_info);
  virtual ~X64Function();

  void* machine_code() const { return machine_code_; }
  size_t code_size() const { return code_size_; }

  void Setup(void* machine_code, size_t code_size);

 protected:
  virtual int AddBreakpointImpl(runtime::Breakpoint* breakpoint);
  virtual int RemoveBreakpointImpl(runtime::Breakpoint* breakpoint);
  virtual int CallImpl(runtime::ThreadState* thread_state,
                       uint64_t return_address);

 private:
  void* machine_code_;
  size_t code_size_;
};

}  // namespace x64
}  // namespace backend
}  // namespace alloy

#endif  // ALLOY_BACKEND_X64_X64_FUNCTION_H_
