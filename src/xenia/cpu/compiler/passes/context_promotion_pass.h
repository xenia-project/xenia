/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_COMPILER_PASSES_CONTEXT_PROMOTION_PASS_H_
#define XENIA_CPU_COMPILER_PASSES_CONTEXT_PROMOTION_PASS_H_

#include <cmath>
#include <vector>

#include "xenia/base/platform.h"
#include "xenia/cpu/compiler/compiler_pass.h"

#if XE_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#include <llvm/ADT/BitVector.h>
#pragma warning(pop)
#else
#include "llvm/ADT/BitVector.h"
#endif  // XE_COMPILER_MSVC

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

class ContextPromotionPass : public CompilerPass {
 public:
  ContextPromotionPass();
  virtual ~ContextPromotionPass() override;

  bool Initialize(Compiler* compiler) override;

  bool Run(hir::HIRBuilder* builder) override;

 private:
  void PromoteBlock(hir::Block* block);
  void RemoveDeadStoresBlock(hir::Block* block);

 private:
  std::vector<hir::Value*> context_values_;
  llvm::BitVector context_validity_;
};

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_COMPILER_PASSES_CONTEXT_PROMOTION_PASS_H_
