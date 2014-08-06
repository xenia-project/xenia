/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_COMPILER_PASSES_CONTROL_FLOW_SIMPLIFICATION_PASS_H_
#define ALLOY_COMPILER_PASSES_CONTROL_FLOW_SIMPLIFICATION_PASS_H_

#include <alloy/compiler/compiler_pass.h>

namespace alloy {
namespace compiler {
namespace passes {

class ControlFlowSimplificationPass : public CompilerPass {
 public:
  ControlFlowSimplificationPass();
  ~ControlFlowSimplificationPass() override;

  int Run(hir::HIRBuilder* builder) override;

 private:
};

}  // namespace passes
}  // namespace compiler
}  // namespace alloy

#endif  // ALLOY_COMPILER_PASSES_CONTROL_FLOW_SIMPLIFICATION_PASS_H_
