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
  if (src1.value) {
    src1.value->RemoveUse(src1_use);
  }
  src1.value = value;
  src1_use = value ? value->AddUse(block->arena, this) : NULL;
}

void Instr::set_src2(Value* value) {
  if (src2.value == value) {
    return;
  }
  if (src2.value) {
    src2.value->RemoveUse(src2_use);
  }
  src2.value = value;
  src2_use = value ? value->AddUse(block->arena, this) : NULL;
}

void Instr::set_src3(Value* value) {
  if (src3.value == value) {
    return;
  }
  if (src3.value) {
    src3.value->RemoveUse(src3_use);
  }
  src3.value = value;
  src3_use = value ? value->AddUse(block->arena, this) : NULL;
}
