/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_FRONTEND_FRONTEND_H_
#define ALLOY_FRONTEND_FRONTEND_H_

#include <memory>

#include <alloy/core.h>
#include <alloy/memory.h>
#include <alloy/frontend/context_info.h>
#include <alloy/runtime/function.h>
#include <alloy/runtime/symbol_info.h>

namespace alloy {
namespace runtime {
class Runtime;
}  // namespace runtime
}  // namespace alloy

namespace alloy {
namespace frontend {

class Frontend {
 public:
  Frontend(runtime::Runtime* runtime);
  virtual ~Frontend();

  runtime::Runtime* runtime() const { return runtime_; }
  Memory* memory() const;
  ContextInfo* context_info() const { return context_info_.get(); }

  virtual int Initialize();

  virtual int DeclareFunction(runtime::FunctionInfo* symbol_info) = 0;
  virtual int DefineFunction(runtime::FunctionInfo* symbol_info,
                             uint32_t debug_info_flags,
                             runtime::Function** out_function) = 0;

 protected:
  runtime::Runtime* runtime_;
  std::unique_ptr<ContextInfo> context_info_;
};

}  // namespace frontend
}  // namespace alloy

#endif  // ALLOY_FRONTEND_FRONTEND_H_
