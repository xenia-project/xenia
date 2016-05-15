/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv/compiler.h"

namespace xe {
namespace gpu {
namespace spirv {

Compiler::Compiler() {}

void Compiler::AddPass(std::unique_ptr<CompilerPass> pass) {
  compiler_passes_.push_back(std::move(pass));
}

bool Compiler::Compile(spv::Module* module) {
  for (auto& pass : compiler_passes_) {
    if (!pass->Run(module)) {
      return false;
    }
  }

  return true;
}

void Compiler::Reset() { compiler_passes_.clear(); }

}  // namespace spirv
}  // namespace gpu
}  // namespace xe