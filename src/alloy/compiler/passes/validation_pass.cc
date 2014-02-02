/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/compiler/passes/validation_pass.h>

#include <alloy/backend/backend.h>
#include <alloy/compiler/compiler.h>
#include <alloy/runtime/runtime.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::compiler;
using namespace alloy::compiler::passes;
using namespace alloy::frontend;
using namespace alloy::hir;
using namespace alloy::runtime;


ValidationPass::ValidationPass() :
    CompilerPass() {
}

ValidationPass::~ValidationPass() {
}

int ValidationPass::Run(HIRBuilder* builder) {
  StringBuffer str;
  builder->Dump(&str);
  printf(str.GetString());
  fflush(stdout);
  str.Reset();

  auto block = builder->first_block();
  while (block) {
    auto label = block->label_head;
    while (label) {
      XEASSERT(label->block == block);
      if (label->block != block) {
        return 1;
      }
      label = label->next;
    }

    auto instr = block->instr_head;
    while (instr) {
      if (ValidateInstruction(block, instr)) {
        return 1;
      }
      instr = instr->next;
    }

    block = block->next;
  }

  return 0;
}

int ValidationPass::ValidateInstruction(Block* block, Instr* instr) {
  XEASSERT(instr->block == block);
  if (instr->block != block) {
    return 1;
  }

  uint32_t signature = instr->opcode->signature;
  if (GET_OPCODE_SIG_TYPE_SRC1(signature) == OPCODE_SIG_TYPE_V) {
    if (ValidateValue(block, instr, instr->src1.value)) {
      return 1;
    }
  }
  if (GET_OPCODE_SIG_TYPE_SRC2(signature) == OPCODE_SIG_TYPE_V) {
    if (ValidateValue(block, instr, instr->src2.value)) {
      return 1;
    }
  }
  if (GET_OPCODE_SIG_TYPE_SRC3(signature) == OPCODE_SIG_TYPE_V) {
    if (ValidateValue(block, instr, instr->src3.value)) {
      return 1;
    }
  }

  return 0;
}

int ValidationPass::ValidateValue(Block* block, Instr* instr, Value* value) {
  if (value->def) {
    /*auto def = value->def;
    XEASSERT(def->block == block);
    if (def->block != block) {
      return 1;
    }*/
  }
  return 0;
}
