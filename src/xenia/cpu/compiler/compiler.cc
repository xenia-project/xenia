/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/compiler.h"

#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/compiler/compiler_pass.h"

namespace xe {
namespace cpu {
namespace compiler {

Compiler::Compiler(Processor* processor) : processor_(processor) {}

Compiler::~Compiler() { Reset(); }

void Compiler::AddPass(std::unique_ptr<CompilerPass> pass) {
  pass->Initialize(this);
  passes_.push_back(std::move(pass));
}

void Compiler::Reset() {}

bool Compiler::Compile(xe::cpu::hir::HIRBuilder* builder) {
  // TODO(benvanik): sophisticated stuff. Run passes in parallel, run until they
  //                 stop changing things, etc.
  XELOGI("Compiler::Compile: Starting compilation with {} passes", passes_.size());
  for (size_t i = 0; i < passes_.size(); ++i) {
    auto& pass = passes_[i];
    XELOGI("Compiler::Compile: Running pass {} of {}: {}", i + 1, passes_.size(), typeid(*pass).name());
    scratch_arena_.Reset();
    if (!pass->Run(builder)) {
      XELOGE("Compiler::Compile: Pass {} failed: {}", i + 1, typeid(*pass).name());
      return false;
    }
    XELOGI("Compiler::Compile: Pass {} completed successfully: {}", i + 1, typeid(*pass).name());
  }

  XELOGI("Compiler::Compile: All {} passes completed successfully", passes_.size());
  return true;
}

}  // namespace compiler
}  // namespace cpu
}  // namespace xe
