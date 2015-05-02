/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/hir/block.h"

#include "xenia/base/assert.h"
#include "xenia/cpu/hir/instr.h"

namespace xe {
namespace cpu {
namespace hir {

void Block::AssertNoCycles() {
  Instr* hare = instr_head;
  Instr* tortoise = instr_head;
  if (!hare) {
    return;
  }
  while ((hare = hare->next)) {
    if (hare == tortoise) {
      // Cycle!
      assert_always();
    }
    hare = hare->next;
    if (hare == tortoise) {
      // Cycle!
      assert_always();
    }
    tortoise = tortoise->next;
    if (!hare || !tortoise) {
      return;
    }
  }
}

}  // namespace hir
}  // namespace cpu
}  // namespace xe
