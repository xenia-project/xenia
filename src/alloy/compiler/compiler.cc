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
#include <alloy/compiler/tracing.h>

using namespace alloy;
using namespace alloy::compiler;
using namespace alloy::hir;
using namespace alloy::runtime;


Compiler::Compiler(Runtime* runtime) :
    runtime_(runtime) {
  scratch_arena_ = new Arena();

  alloy::tracing::WriteEvent(EventType::Init({
  }));
}

Compiler::~Compiler() {
  Reset();

  for (auto it = passes_.begin(); it != passes_.end(); ++it) {
    CompilerPass* pass = *it;
    delete pass;
  }

  delete scratch_arena_;

  alloy::tracing::WriteEvent(EventType::Deinit({
  }));
}

void Compiler::AddPass(CompilerPass* pass) {
  pass->Initialize(this);
  passes_.push_back(pass);
}

void Compiler::Reset() {
}

int Compiler::Compile(HIRBuilder* builder) {
  // TODO(benvanik): sophisticated stuff. Run passes in parallel, run until they
  //                 stop changing things, etc.
  for (auto it = passes_.begin(); it != passes_.end(); ++it) {
    CompilerPass* pass = *it;
    scratch_arena_->Reset();
    if (pass->Run(builder)) {
      return 1;
    }
  }

  return 0;
}
