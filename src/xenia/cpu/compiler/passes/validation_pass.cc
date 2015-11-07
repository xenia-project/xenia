/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/validation_pass.h"

#include "xenia/base/assert.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/backend/backend.h"
#include "xenia/cpu/compiler/compiler.h"
#include "xenia/cpu/processor.h"

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::Block;
using xe::cpu::hir::HIRBuilder;
using xe::cpu::hir::Instr;
using xe::cpu::hir::OpcodeSignatureType;
using xe::cpu::hir::Value;

ValidationPass::ValidationPass() : CompilerPass() {}

ValidationPass::~ValidationPass() {}

bool ValidationPass::Run(HIRBuilder* builder) {
#if 0
  StringBuffer str;
  builder->Dump(&str);
  printf("%s", str.GetString());
  fflush(stdout);
  str.Reset();
#endif  // 0

  auto block = builder->first_block();
  while (block) {
    auto label = block->label_head;
    while (label) {
      assert_true(label->block == block);
      if (label->block != block) {
        return false;
      }
      label = label->next;
    }

    auto instr = block->instr_head;
    while (instr) {
      if (!ValidateInstruction(block, instr)) {
        return false;
      }
      instr = instr->next;
    }

    block = block->next;
  }

  return true;
}

bool ValidationPass::ValidateInstruction(Block* block, Instr* instr) {
  assert_true(instr->block == block);
  if (instr->block != block) {
    return false;
  }

  if (instr->dest) {
    assert_true(instr->dest->def == instr);
    auto use = instr->dest->use_head;
    while (use) {
      assert_true(use->instr->block == block);
      use = use->next;
    }
  }

  uint32_t signature = instr->opcode->signature;
  if (GET_OPCODE_SIG_TYPE_SRC1(signature) == OPCODE_SIG_TYPE_V) {
    if (!ValidateValue(block, instr, instr->src1.value)) {
      return false;
    }
  }
  if (GET_OPCODE_SIG_TYPE_SRC2(signature) == OPCODE_SIG_TYPE_V) {
    if (!ValidateValue(block, instr, instr->src2.value)) {
      return false;
    }
  }
  if (GET_OPCODE_SIG_TYPE_SRC3(signature) == OPCODE_SIG_TYPE_V) {
    if (!ValidateValue(block, instr, instr->src3.value)) {
      return false;
    }
  }

  return true;
}

bool ValidationPass::ValidateValue(Block* block, Instr* instr, Value* value) {
  // if (value->def) {
  //  auto def = value->def;
  //  assert_true(def->block == block);
  //  if (def->block != block) {
  //    return false;
  //  }
  //}
  return true;
}

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe
