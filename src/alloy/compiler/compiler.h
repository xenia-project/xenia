/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_COMPILER_COMPILER_H_
#define ALLOY_COMPILER_COMPILER_H_

#include <memory>
#include <vector>

#include <alloy/core.h>
#include <alloy/hir/hir_builder.h>

namespace alloy {
namespace runtime {
class Runtime;
}  // namespace runtime
}  // namespace alloy

namespace alloy {
namespace compiler {

class CompilerPass;

class Compiler {
 public:
  Compiler(runtime::Runtime* runtime);
  ~Compiler();

  runtime::Runtime* runtime() const { return runtime_; }
  Arena* scratch_arena() { return &scratch_arena_; }

  void AddPass(std::unique_ptr<CompilerPass> pass);

  void Reset();

  int Compile(hir::HIRBuilder* builder);

 private:
  runtime::Runtime* runtime_;
  Arena scratch_arena_;

  std::vector<std::unique_ptr<CompilerPass>> passes_;
};

}  // namespace compiler
}  // namespace alloy

#endif  // ALLOY_COMPILER_COMPILER_H_
