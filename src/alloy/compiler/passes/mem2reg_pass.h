/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_COMPILER_PASSES_MEM2REG_PASS_H_
#define ALLOY_COMPILER_PASSES_MEM2REG_PASS_H_

#include <alloy/compiler/pass.h>


namespace alloy {
namespace compiler {
namespace passes {


class Mem2RegPass : public Pass {
public:
  Mem2RegPass();
  virtual ~Mem2RegPass();
};


}  // namespace passes
}  // namespace compiler
}  // namespace alloy


#endif  // ALLOY_COMPILER_PASSES_MEM2REG_PASS_H_
