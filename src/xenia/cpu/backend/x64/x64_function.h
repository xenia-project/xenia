/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BACKEND_X64_X64_FUNCTION_H_
#define XENIA_BACKEND_X64_X64_FUNCTION_H_

#include "xenia/cpu/function.h"
#include "xenia/cpu/symbol_info.h"
#include "xenia/cpu/thread_state.h"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

class X64Function : public Function {
 public:
  X64Function(FunctionInfo* symbol_info);
  virtual ~X64Function();

  uint8_t* machine_code() const override { return machine_code_; }
  size_t machine_code_length() const override { return machine_code_length_; }

  void Setup(uint8_t* machine_code, size_t machine_code_length);

 protected:
  bool AddBreakpointImpl(debug::Breakpoint* breakpoint) override;
  bool RemoveBreakpointImpl(debug::Breakpoint* breakpoint) override;
  bool CallImpl(ThreadState* thread_state, uint32_t return_address) override;

 private:
  uint8_t* machine_code_;
  size_t machine_code_length_;
};

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_BACKEND_X64_X64_FUNCTION_H_
