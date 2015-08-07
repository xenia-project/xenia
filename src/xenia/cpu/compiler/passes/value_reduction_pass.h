/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_COMPILER_PASSES_VALUE_REDUCTION_PASS_H_
#define XENIA_CPU_COMPILER_PASSES_VALUE_REDUCTION_PASS_H_

#include "xenia/cpu/compiler/compiler_pass.h"

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

class ValueReductionPass : public CompilerPass {
 public:
  ValueReductionPass();
  ~ValueReductionPass() override;

  bool Run(hir::HIRBuilder* builder) override;

 private:
  void ComputeLastUse(hir::Value* value);
};

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_COMPILER_PASSES_VALUE_REDUCTION_PASS_H_
