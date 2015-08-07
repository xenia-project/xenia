/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_COMPILER_COMPILER_H_
#define XENIA_CPU_COMPILER_COMPILER_H_

#include <memory>
#include <vector>

#include "xenia/base/arena.h"
#include "xenia/cpu/hir/hir_builder.h"

namespace xe {
namespace cpu {
class Processor;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace cpu {
namespace compiler {

class CompilerPass;

class Compiler {
 public:
  explicit Compiler(Processor* processor);
  ~Compiler();

  Processor* processor() const { return processor_; }
  Arena* scratch_arena() { return &scratch_arena_; }

  void AddPass(std::unique_ptr<CompilerPass> pass);

  void Reset();

  bool Compile(hir::HIRBuilder* builder);

 private:
  Processor* processor_;
  Arena scratch_arena_;

  std::vector<std::unique_ptr<CompilerPass>> passes_;
};

}  // namespace compiler
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_COMPILER_COMPILER_H_
