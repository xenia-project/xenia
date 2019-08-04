/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_COMPILER_PASSES_CONDITIONAL_GROUP_PASS_H_
#define XENIA_CPU_COMPILER_PASSES_CONDITIONAL_GROUP_PASS_H_

#include <cmath>
#include <vector>

#include "xenia/base/platform.h"
#include "xenia/cpu/compiler/compiler_pass.h"
#include "xenia/cpu/compiler/passes/conditional_group_subpass.h"

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

class ConditionalGroupPass : public CompilerPass {
 public:
  ConditionalGroupPass();
  virtual ~ConditionalGroupPass() override;

  bool Initialize(Compiler* compiler) override;

  bool Run(hir::HIRBuilder* builder) override;

  void AddPass(std::unique_ptr<CompilerPass> pass);

 private:
  std::vector<std::unique_ptr<CompilerPass>> passes_;
};

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_COMPILER_PASSES_CONDITIONAL_GROUP_PASS_H_
