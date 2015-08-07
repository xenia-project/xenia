/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_COMPILER_PASSES_MEMORY_SEQUENCE_COMBINATION_PASS_H_
#define XENIA_CPU_COMPILER_PASSES_MEMORY_SEQUENCE_COMBINATION_PASS_H_

#include "xenia/cpu/compiler/compiler_pass.h"

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

class MemorySequenceCombinationPass : public CompilerPass {
 public:
  MemorySequenceCombinationPass();
  ~MemorySequenceCombinationPass() override;

  bool Run(hir::HIRBuilder* builder) override;

 private:
  void CombineMemorySequences(hir::HIRBuilder* builder);
  void CombineLoadSequence(hir::Instr* i);
  void CombineStoreSequence(hir::Instr* i);
};

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_COMPILER_PASSES_MEMORY_SEQUENCE_COMBINATION_PASS_H_
