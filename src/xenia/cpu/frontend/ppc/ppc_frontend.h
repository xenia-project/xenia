/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_FRONTEND_PPC_PPC_FRONTEND_H_
#define XENIA_FRONTEND_PPC_PPC_FRONTEND_H_

#include <mutex>

#include "xenia/cpu/frontend/frontend.h"
#include "poly/type_pool.h"

namespace xe {
namespace cpu {
namespace frontend {
namespace ppc {

class PPCTranslator;

struct PPCBuiltins {
  std::mutex global_lock;
  bool global_lock_taken;
  FunctionInfo* check_global_lock;
  FunctionInfo* handle_global_lock;
};

class PPCFrontend : public Frontend {
 public:
  PPCFrontend(Runtime* runtime);
  ~PPCFrontend() override;

  int Initialize() override;

  PPCBuiltins* builtins() { return &builtins_; }

  int DeclareFunction(FunctionInfo* symbol_info) override;
  int DefineFunction(FunctionInfo* symbol_info, uint32_t debug_info_flags,
                     uint32_t trace_flags, Function** out_function) override;

 private:
  poly::TypePool<PPCTranslator, PPCFrontend*> translator_pool_;
  PPCBuiltins builtins_;
};

}  // namespace ppc
}  // namespace frontend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_FRONTEND_PPC_PPC_FRONTEND_H_
