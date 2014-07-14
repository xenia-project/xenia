/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_COMPILER_PASSES_CONSTANT_PROPAGATION_PASS_H_
#define ALLOY_COMPILER_PASSES_CONSTANT_PROPAGATION_PASS_H_

#include <alloy/compiler/compiler_pass.h>

namespace alloy {
namespace compiler {
namespace passes {

class ConstantPropagationPass : public CompilerPass {
 public:
  ConstantPropagationPass();
  ~ConstantPropagationPass() override;

  int Run(hir::HIRBuilder* builder) override;

 private:
  void PropagateCarry(hir::Value* v, bool did_carry);
};

}  // namespace passes
}  // namespace compiler
}  // namespace alloy

#endif  // ALLOY_COMPILER_PASSES_CONSTANT_PROPAGATION_PASS_H_
