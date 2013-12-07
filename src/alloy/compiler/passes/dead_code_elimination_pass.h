/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_COMPILER_PASSES_DEAD_CODE_ELIMINATION_PASS_H_
#define ALLOY_COMPILER_PASSES_DEAD_CODE_ELIMINATION_PASS_H_

#include <alloy/compiler/pass.h>


namespace alloy {
namespace compiler {
namespace passes {


class DeadCodeEliminationPass : public Pass {
public:
  DeadCodeEliminationPass();
  virtual ~DeadCodeEliminationPass();

  virtual int Run(hir::FunctionBuilder* builder);

private:
  void MakeNopRecursive(const hir::OpcodeInfo* nop, hir::Instr* i);
};


}  // namespace passes
}  // namespace compiler
}  // namespace alloy


#endif  // ALLOY_COMPILER_PASSES_DEAD_CODE_ELIMINATION_PASS_H_
