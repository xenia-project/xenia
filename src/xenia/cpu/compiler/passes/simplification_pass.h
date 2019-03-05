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

#include "xenia/cpu/compiler/passes/conditional_group_subpass.h"

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

class SimplificationPass : public ConditionalGroupSubpass {
 public:
  SimplificationPass();
  ~SimplificationPass() override;

  bool Run(hir::HIRBuilder* builder, bool& result) override;

 private:
  bool EliminateConversions(hir::HIRBuilder* builder);
  bool CheckTruncate(hir::Instr* i);
  bool CheckByteSwap(hir::Instr* i);

  bool SimplifyAssignments(hir::HIRBuilder* builder);
  hir::Value* CheckValue(hir::Value* value, bool& result);
};

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_COMPILER_PASSES_SIMPLIFICATION_PASS_H_
