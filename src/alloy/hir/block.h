/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_HIR_BLOCK_H_
#define ALLOY_HIR_BLOCK_H_

#include <alloy/core.h>


namespace alloy {
namespace hir {

class Instr;
class Label;


class Block {
public:
  Block* next;
  Block* prev;

  Label* label_head;
  Label* label_tail;

  Instr* instr_head;
  Instr* instr_tail;
};


}  // namespace hir
}  // namespace alloy


#endif  // ALLOY_HIR_BLOCK_H_
