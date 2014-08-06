/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/compiler/compiler.h>

#include <alloy/compiler/compiler_pass.h>

namespace alloy {
namespace compiler {

using alloy::hir::HIRBuilder;
using alloy::runtime::Runtime;

Compiler::Compiler(Runtime* runtime) : runtime_(runtime) {}

Compiler::~Compiler() { Reset(); }

void Compiler::AddPass(std::unique_ptr<CompilerPass> pass) {
  pass->Initialize(this);
  passes_.push_back(std::move(pass));
}

void Compiler::Reset() {}

int Compiler::Compile(HIRBuilder* builder) {
  SCOPE_profile_cpu_f("alloy");

  // TODO(benvanik): sophisticated stuff. Run passes in parallel, run until they
  //                 stop changing things, etc.
  for (size_t i = 0; i < passes_.size(); ++i) {
    auto& pass = passes_[i];
    scratch_arena_.Reset();
    if (pass->Run(builder)) {
      return 1;
    }
  }

  return 0;
}

}  // namespace compiler
}  // namespace alloy
