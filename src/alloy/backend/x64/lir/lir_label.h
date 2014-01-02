/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_LIR_LIR_LABEL_H_
#define ALLOY_BACKEND_X64_LIR_LIR_LABEL_H_

#include <alloy/core.h>


namespace alloy {
namespace backend {
namespace x64 {
namespace lir {

class LIRBlock;


class LIRLabel {
public:
  LIRBlock* block;
  LIRLabel* next;
  LIRLabel* prev;

  uint32_t  id;
  char*     name;
  bool      local;

  void*     tag;
};


}  // namespace lir
}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_LIR_LIR_LABEL_H_
