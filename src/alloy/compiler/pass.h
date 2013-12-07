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


namespace alloy {
namespace compiler {


class Pass {
public:
  Pass();
  virtual ~Pass();
};


}  // namespace compiler
}  // namespace alloy


#endif  // ALLOY_COMPILER_PASS_H_
