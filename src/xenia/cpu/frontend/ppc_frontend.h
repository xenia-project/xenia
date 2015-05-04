/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_FRONTEND_PPC_FRONTEND_H_
#define XENIA_FRONTEND_PPC_FRONTEND_H_

#include <memory>
#include <mutex>

#include "xenia/base/type_pool.h"
#include "xenia/cpu/frontend/context_info.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/symbol_info.h"
#include "xenia/memory.h"

namespace xe {
namespace cpu {
class Processor;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace cpu {
namespace frontend {

class PPCTranslator;

struct PPCBuiltins {
  std::mutex global_lock;
  bool global_lock_taken;
  FunctionInfo* check_global_lock;
  FunctionInfo* handle_global_lock;
};

class PPCFrontend {
 public:
  explicit PPCFrontend(Processor* processor);
  ~PPCFrontend();

  int Initialize();

  Processor* processor() const { return processor_; }
  Memory* memory() const;
  ContextInfo* context_info() const { return context_info_.get(); }
  PPCBuiltins* builtins() { return &builtins_; }

  int DeclareFunction(FunctionInfo* symbol_info);
  int DefineFunction(FunctionInfo* symbol_info, uint32_t debug_info_flags,
                     uint32_t trace_flags, Function** out_function);

 private:
  Processor* processor_;
  std::unique_ptr<ContextInfo> context_info_;
  PPCBuiltins builtins_;
  TypePool<PPCTranslator, PPCFrontend*> translator_pool_;
};

}  // namespace frontend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_FRONTEND_PPC_FRONTEND_H_
