/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_COMPILER_PASSES_CONTEXT_PROMOTION_PASS_H_
#define ALLOY_COMPILER_PASSES_CONTEXT_PROMOTION_PASS_H_

#include <alloy/compiler/compiler_pass.h>

namespace alloy {
namespace compiler {
namespace passes {

class ContextPromotionPass : public CompilerPass {
 public:
  ContextPromotionPass();
  virtual ~ContextPromotionPass() override;

  int Initialize(Compiler* compiler) override;

  int Run(hir::HIRBuilder* builder) override;

 private:
  void PromoteBlock(hir::Block* block);
  void RemoveDeadStoresBlock(hir::Block* block);

 private:
  size_t context_values_size_;
  hir::Value** context_values_;
};

}  // namespace passes
}  // namespace compiler
}  // namespace alloy

#endif  // ALLOY_COMPILER_PASSES_CONTEXT_PROMOTION_PASS_H_
