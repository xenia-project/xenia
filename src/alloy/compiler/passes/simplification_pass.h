/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_COMPILER_PASSES_SIMPLIFICATION_PASS_H_
#define ALLOY_COMPILER_PASSES_SIMPLIFICATION_PASS_H_

#include <alloy/compiler/pass.h>


namespace alloy {
namespace compiler {
namespace passes {


class SimplificationPass : public Pass {
public:
  SimplificationPass();
  virtual ~SimplificationPass();

  virtual int Run(hir::FunctionBuilder* builder);

private:
  hir::Value* CheckValue(hir::Value* value);
};


}  // namespace passes
}  // namespace compiler
}  // namespace alloy


#endif  // ALLOY_COMPILER_PASSES_SIMPLIFICATION_PASS_H_
