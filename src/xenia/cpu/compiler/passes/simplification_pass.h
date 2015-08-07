/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_COMPILER_PASSES_SIMPLIFICATION_PASS_H_
#define XENIA_CPU_COMPILER_PASSES_SIMPLIFICATION_PASS_H_

#include "xenia/cpu/compiler/compiler_pass.h"

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

class SimplificationPass : public CompilerPass {
 public:
  SimplificationPass();
  ~SimplificationPass() override;

  bool Run(hir::HIRBuilder* builder) override;

 private:
  void EliminateConversions(hir::HIRBuilder* builder);
  void CheckTruncate(hir::Instr* i);
  void CheckByteSwap(hir::Instr* i);

  void SimplifyAssignments(hir::HIRBuilder* builder);
  hir::Value* CheckValue(hir::Value* value);
};

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_COMPILER_PASSES_SIMPLIFICATION_PASS_H_
