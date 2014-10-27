/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_FRONTEND_PPC_PPC_FRONTEND_H_
#define ALLOY_FRONTEND_PPC_PPC_FRONTEND_H_

#include <mutex>

#include <alloy/frontend/frontend.h>
#include <alloy/type_pool.h>

namespace alloy {
namespace frontend {
namespace ppc {

class PPCTranslator;

struct PPCBuiltins {
  std::mutex global_lock;
  bool global_lock_taken;
  runtime::FunctionInfo* check_global_lock;
  runtime::FunctionInfo* handle_global_lock;
};

class PPCFrontend : public Frontend {
 public:
  PPCFrontend(runtime::Runtime* runtime);
  ~PPCFrontend() override;

  int Initialize() override;

  PPCBuiltins* builtins() { return &builtins_; }

  int DeclareFunction(runtime::FunctionInfo* symbol_info) override;
  int DefineFunction(runtime::FunctionInfo* symbol_info,
                     uint32_t debug_info_flags, uint32_t trace_flags,
                     runtime::Function** out_function) override;

 private:
  TypePool<PPCTranslator, PPCFrontend*> translator_pool_;
  PPCBuiltins builtins_;
};

}  // namespace ppc
}  // namespace frontend
}  // namespace alloy

#endif  // ALLOY_FRONTEND_PPC_PPC_FRONTEND_H_
