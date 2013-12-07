/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_IVM_IVM_FUNCTION_H_
#define ALLOY_BACKEND_IVM_IVM_FUNCTION_H_

#include <alloy/core.h>
#include <alloy/backend/ivm/ivm_intcode.h>
#include <alloy/runtime/function.h>
#include <alloy/runtime/symbol_info.h>


namespace alloy {
namespace backend {
namespace ivm {


class IVMFunction : public runtime::GuestFunction {
public:
  IVMFunction(runtime::FunctionInfo* symbol_info);
  virtual ~IVMFunction();

  void Setup(TranslationContext& ctx);

protected:
  virtual int CallImpl(runtime::ThreadState* thread_state);

private:

private:
  size_t    register_count_;
  Register* constant_regiters_;
  size_t    intcode_count_;
  IntCode*  intcodes_;
  // ... source_map_;
};


}  // namespace ivm
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_IVM_IVM_FUNCTION_H_
