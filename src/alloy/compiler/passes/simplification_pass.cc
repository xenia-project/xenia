/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/compiler/passes/simplification_pass.h>
#include <alloy/hir/function_builder.h>

using namespace alloy;
using namespace alloy::compiler;
using namespace alloy::compiler::passes;
using namespace alloy::hir;


SimplificationPass::SimplificationPass() :
    Pass() {
}

SimplificationPass::~SimplificationPass() {
}

int SimplificationPass::Run(FunctionBuilder* builder) {
  // Run over the instructions and rename assigned variables:
  //   v1 = v0
  //   v2 = v1
  //   v3 = add v0, v2
  // becomes:
  //   v1 = v0
  //   v2 = v0
  //   v3 = add v0, v0
  // This could be run several times, as it could make other passes faster
  // to compute (for example, ConstantPropagation). DCE will take care of
  // the useless assigns.
  //
  // We do this by walking each instruction. For each value op we
  // look at its def instr to see if it's an assign - if so, we use the src
  // of that instr. Because we may have chains, we do this recursively until
  // we find a non-assign def.

  Block* block = builder->first_block();
  while (block) {
    Instr* i = block->instr_head;
    while (i) {
      uint32_t signature = i->opcode->signature;
      if (GET_OPCODE_SIG_TYPE_SRC1(signature) == OPCODE_SIG_TYPE_V) {
        i->set_src1(CheckValue(i->src1.value));
      }
      if (GET_OPCODE_SIG_TYPE_SRC2(signature) == OPCODE_SIG_TYPE_V) {
        i->set_src2(CheckValue(i->src2.value));
      }
      if (GET_OPCODE_SIG_TYPE_SRC3(signature) == OPCODE_SIG_TYPE_V) {
        i->set_src3(CheckValue(i->src3.value));
      }
      i = i->next;
    }

    block = block->next;
  }

  return 0;
}

Value* SimplificationPass::CheckValue(Value* value) {
  Instr* def = value->def;
  if (def && def->opcode->num == OPCODE_ASSIGN) {
    // Value comes from an assignment - recursively find if it comes from
    // another assignment. It probably doesn't, if we already replaced it.
    Value* replacement = def->src1.value;
    while (true) {
      def = replacement->def;
      if (!def || def->opcode->num != OPCODE_ASSIGN) {
        break;
      }
      replacement = def->src1.value;
    }
    return replacement;
  }
  return value;
}
