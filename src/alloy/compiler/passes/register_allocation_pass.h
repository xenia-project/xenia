/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_COMPILER_PASSES_REGISTER_ALLOCATION_PASS_H_
#define ALLOY_COMPILER_PASSES_REGISTER_ALLOCATION_PASS_H_

#include <alloy/compiler/compiler_pass.h>

XEDECLARECLASS2(alloy, backend, Backend);


namespace alloy {
namespace compiler {
namespace passes {


class RegisterAllocationPass : public CompilerPass {
public:
  RegisterAllocationPass(backend::Backend* backend);
  virtual ~RegisterAllocationPass();

  virtual int Run(hir::HIRBuilder* builder);

private:
  int ProcessBlock(hir::Block* block);
};


}  // namespace passes
}  // namespace compiler
}  // namespace alloy


#endif  // ALLOY_COMPILER_PASSES_REGISTER_ALLOCATION_PASS_H_
