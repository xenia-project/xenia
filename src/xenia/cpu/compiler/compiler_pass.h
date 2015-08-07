/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_COMPILER_COMPILER_PASS_H_
#define XENIA_CPU_COMPILER_COMPILER_PASS_H_

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

class Compiler;

class CompilerPass {
 public:
  CompilerPass();
  virtual ~CompilerPass();

  virtual bool Initialize(Compiler* compiler);

  virtual bool Run(hir::HIRBuilder* builder) = 0;

 protected:
  Arena* scratch_arena() const;

 protected:
  Processor* processor_;
  Compiler* compiler_;
};

}  // namespace compiler
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_COMPILER_COMPILER_PASS_H_
