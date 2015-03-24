/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_FRONTEND_FRONTEND_H_
#define XENIA_FRONTEND_FRONTEND_H_

#include <memory>

#include "xenia/cpu/frontend/context_info.h"
#include "xenia/memory.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/symbol_info.h"

namespace xe {
namespace cpu {
class Runtime;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace cpu {
namespace frontend {

class Frontend {
 public:
  Frontend(Runtime* runtime);
  virtual ~Frontend();

  Runtime* runtime() const { return runtime_; }
  Memory* memory() const;
  ContextInfo* context_info() const { return context_info_.get(); }

  virtual int Initialize();

  virtual int DeclareFunction(FunctionInfo* symbol_info) = 0;
  virtual int DefineFunction(FunctionInfo* symbol_info,
                             uint32_t debug_info_flags, uint32_t trace_flags,
                             Function** out_function) = 0;

 protected:
  Runtime* runtime_;
  std::unique_ptr<ContextInfo> context_info_;
};

}  // namespace frontend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_FRONTEND_FRONTEND_H_
