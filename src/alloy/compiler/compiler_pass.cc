/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/compiler/compiler_pass.h>

#include <alloy/compiler/compiler.h>

namespace alloy {
namespace compiler {

CompilerPass::CompilerPass() : runtime_(0), compiler_(0) {}

CompilerPass::~CompilerPass() = default;

int CompilerPass::Initialize(Compiler* compiler) {
  runtime_ = compiler->runtime();
  compiler_ = compiler;
  return 0;
}

Arena* CompilerPass::scratch_arena() const {
  return compiler_->scratch_arena();
}

}  // namespace compiler
}  // namespace alloy
