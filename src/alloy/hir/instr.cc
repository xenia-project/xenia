/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/hir/instr.h>

#include <alloy/hir/block.h>

using namespace alloy;
using namespace alloy::hir;


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

void Instr::Replace(const OpcodeInfo* opcode, uint16_t flags) {
  this->opcode = opcode;
  this->flags = flags;

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
