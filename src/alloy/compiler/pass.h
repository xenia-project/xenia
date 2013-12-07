/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_COMPILER_PASS_H_
#define ALLOY_COMPILER_PASS_H_

#include <alloy/core.h>

#include <alloy/hir/function_builder.h>


namespace alloy {
namespace compiler {


class Pass {
public:
  Pass();
  virtual ~Pass();

  virtual int Run(hir::FunctionBuilder* builder) = 0;
};


}  // namespace compiler
}  // namespace alloy


#endif  // ALLOY_COMPILER_PASS_H_
