/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/conditional_group_pass.h"

#include "xenia/base/profiling.h"
#include "xenia/cpu/compiler/compiler.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::Block;
using xe::cpu::hir::HIRBuilder;
using xe::cpu::hir::Instr;
using xe::cpu::hir::Value;

ConditionalGroupPass::ConditionalGroupPass() : CompilerPass() {}

ConditionalGroupPass::~ConditionalGroupPass() {}

bool ConditionalGroupPass::Initialize(Compiler* compiler) {
  if (!CompilerPass::Initialize(compiler)) {
    return false;
  }

  for (size_t i = 0; i < passes_.size(); ++i) {
    auto& pass = passes_[i];
    if (!pass->Initialize(compiler)) {
      return false;
    }
  }

  return true;
}

bool ConditionalGroupPass::Run(HIRBuilder* builder) {
  bool dirty;
  do {
    dirty = false;
    for (size_t i = 0; i < passes_.size(); ++i) {
      scratch_arena()->Reset();
      auto& pass = passes_[i];
      auto subpass = dynamic_cast<ConditionalGroupSubpass*>(pass.get());
      if (!subpass) {
        if (!pass->Run(builder)) {
          return false;
        }
      } else {
        bool result = false;
        if (!subpass->Run(builder, result)) {
          return false;
        }
        dirty |= result;
      }
    }
  } while (dirty);
  return true;
}

void ConditionalGroupPass::AddPass(std::unique_ptr<CompilerPass> pass) {
  passes_.push_back(std::move(pass));
}

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe
