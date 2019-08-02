/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_COMPILER_PASSES_CONDITIONAL_GROUP_SUBPASS_H_
#define XENIA_CPU_COMPILER_PASSES_CONDITIONAL_GROUP_SUBPASS_H_

#include "xenia/base/arena.h"
#include "xenia/cpu/compiler/compiler_pass.h"
#include "xenia/cpu/hir/hir_builder.h"

namespace xe {
namespace cpu {
class Processor;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace cpu {
namespace compiler {
class Compiler;
namespace passes {

class ConditionalGroupSubpass : public CompilerPass {
 public:
  ConditionalGroupSubpass();
  virtual ~ConditionalGroupSubpass();

  bool Run(hir::HIRBuilder* builder) override {
    bool dummy;
    return Run(builder, dummy);
  }

  virtual bool Run(hir::HIRBuilder* builder, bool& result) = 0;
};

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_COMPILER_PASSES_CONDITIONAL_GROUP_SUBPASS_H_
