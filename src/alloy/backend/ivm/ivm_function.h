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


class IVMFunction : public runtime::Function {
public:
  IVMFunction(runtime::FunctionInfo* symbol_info);
  virtual ~IVMFunction();

  void Setup(TranslationContext& ctx);

protected:
  virtual int AddBreakpointImpl(runtime::Breakpoint* breakpoint);
  virtual int RemoveBreakpointImpl(runtime::Breakpoint* breakpoint);
  virtual int CallImpl(runtime::ThreadState* thread_state,
                       uint64_t return_address);

private:
  IntCode* GetIntCodeAtSourceOffset(uint64_t offset);
  void OnBreakpointHit(runtime::ThreadState* thread_state, IntCode* i);

private:
  size_t          register_count_;
  size_t          stack_size_;
  size_t          intcode_count_;
  IntCode*        intcodes_;
  size_t          source_map_count_;
  SourceMapEntry* source_map_;
};


}  // namespace ivm
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_IVM_IVM_FUNCTION_H_
