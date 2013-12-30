/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_LIR_LIR_BLOCK_H_
#define ALLOY_BACKEND_X64_LIR_LIR_BLOCK_H_

#include <alloy/core.h>


namespace alloy {
namespace backend {
namespace x64 {
namespace lir {

class LIRBuilder;
class LIRInstr;
class LIRLabel;


class LIRBlock {
public:
  Arena* arena;

  LIRBlock* next;
  LIRBlock* prev;

  LIRLabel* label_head;
  LIRLabel* label_tail;

  LIRInstr* instr_head;
  LIRInstr* instr_tail;
};


}  // namespace lir
}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_LIR_LIR_BLOCK_H_
