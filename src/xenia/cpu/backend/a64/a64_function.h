/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_FUNCTION_H_
#define XENIA_CPU_BACKEND_A64_A64_FUNCTION_H_

#include "xenia/cpu/function.h"
#include "xenia/cpu/thread_state.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

class A64Function : public GuestFunction {
 public:
  A64Function(Module* module, uint32_t address);
  ~A64Function() override;

  uint8_t* machine_code() const override { return machine_code_; }
  size_t machine_code_length() const override { return machine_code_length_; }

  void Setup(uint8_t* machine_code, size_t machine_code_length);

 protected:
  bool CallImpl(ThreadState* thread_state, uint32_t return_address) override;

 private:
  uint8_t* machine_code_ = nullptr;
  size_t machine_code_length_ = 0;
};

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_FUNCTION_H_
