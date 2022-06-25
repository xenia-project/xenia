/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/hir/instr.h"

#include "xenia/cpu/hir/block.h"

namespace xe {
namespace cpu {
namespace hir {

void Instr::set_src1(Value* value) {
  if (src1.value == value) {
    return;
  }
  if (src1_use) {
    src1.value->RemoveUse(src1_use);
  }
  src1.value = value;
  src1_use = value ? value->AddUse(block->arena, this) : NULL;
}

void Instr::set_src2(Value* value) {
  if (src2.value == value) {
    return;
  }
  if (src2_use) {
    src2.value->RemoveUse(src2_use);
  }
  src2.value = value;
  src2_use = value ? value->AddUse(block->arena, this) : NULL;
}

void Instr::set_src3(Value* value) {
  if (src3.value == value) {
    return;
  }
  if (src3_use) {
    src3.value->RemoveUse(src3_use);
  }
  src3.value = value;
  src3_use = value ? value->AddUse(block->arena, this) : NULL;
}

void Instr::MoveBefore(Instr* other) {
  if (next == other) {
    return;
  }

  // Remove from current location.
  if (prev) {
    prev->next = next;
  } else {
    block->instr_head = next;
  }
  if (next) {
    next->prev = prev;
  } else {
    block->instr_tail = prev;
  }

  // Insert into new location.
  block = other->block;
  next = other;
  prev = other->prev;
  other->prev = this;
  if (prev) {
    prev->next = this;
  }
  if (other == block->instr_head) {
    block->instr_head = this;
  }
}

void Instr::Replace(const OpcodeInfo* new_opcode, uint16_t new_flags) {
  opcode = new_opcode;
  flags = new_flags;

  if (src1_use) {
    src1.value->RemoveUse(src1_use);
    src1.value = NULL;
    src1_use = NULL;
  }
  if (src2_use) {
    src2.value->RemoveUse(src2_use);
    src2.value = NULL;
    src2_use = NULL;
  }
  if (src3_use) {
    src3.value->RemoveUse(src3_use);
    src3.value = NULL;
    src3_use = NULL;
  }
}

void Instr::Remove() {
  // Remove all srcs/dest.
  Replace(&OPCODE_NOP_info, 0);

  if (prev) {
    prev->next = next;
  } else {
    block->instr_head = next;
  }
  if (next) {
    next->prev = prev;
  } else {
    block->instr_tail = prev;
  }
}
Instr* Instr::GetDestDefSkipAssigns() {
  Instr* current_def = this;

  while (current_def->opcode == &OPCODE_ASSIGN_info) {
    Instr* next_def = current_def->src1.value->def;

    if (!next_def) {
      return nullptr;
    }

    current_def = next_def;
  }
  return current_def;
}
}  // namespace hir
}  // namespace cpu
}  // namespace xe
