/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_COMPILER_PASSES_DEAD_CODE_ELIMINATION_PASS_H_
#define XENIA_CPU_COMPILER_PASSES_DEAD_CODE_ELIMINATION_PASS_H_

#include "xenia/cpu/compiler/compiler_pass.h"

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

class DeadCodeEliminationPass : public CompilerPass {
 public:
  DeadCodeEliminationPass();
  ~DeadCodeEliminationPass() override;

  bool Run(hir::HIRBuilder* builder) override;

 private:
  void MakeNopRecursive(hir::Instr* i);
  void ReplaceAssignment(hir::Instr* i);
  bool CheckLocalUse(hir::Instr* i);
};

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_COMPILER_PASSES_DEAD_CODE_ELIMINATION_PASS_H_
