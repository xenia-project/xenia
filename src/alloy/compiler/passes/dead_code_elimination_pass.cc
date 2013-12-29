/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/compiler/passes/dead_code_elimination_pass.h>

using namespace alloy;
using namespace alloy::compiler;
using namespace alloy::compiler::passes;
using namespace alloy::hir;


DeadCodeEliminationPass::DeadCodeEliminationPass() :
    CompilerPass() {
}

DeadCodeEliminationPass::~DeadCodeEliminationPass() {
}

int DeadCodeEliminationPass::Run(HIRBuilder* builder) {
  // ContextPromotion/DSE will likely leave around a lot of dead statements.
  // Code generated for comparison/testing produces many unused statements and
  // with proper use analysis it should be possible to remove most of them:
  // After context promotion/simplification:
  //   v33.i8 = compare_ult v31.i32, 0
  //   v34.i8 = compare_ugt v31.i32, 0
  //   v35.i8 = compare_eq v31.i32, 0
  //   store_context +300, v33.i8
  //   store_context +301, v34.i8
  //   store_context +302, v35.i8
  //   branch_true v35.i8, loc_8201A484
  // After DSE:
  //   v33.i8 = compare_ult v31.i32, 0
  //   v34.i8 = compare_ugt v31.i32, 0
  //   v35.i8 = compare_eq v31.i32, 0
  //   branch_true v35.i8, loc_8201A484
  // After DCE:
  //   v35.i8 = compare_eq v31.i32, 0
  //   branch_true v35.i8, loc_8201A484

  // We process DCE by reverse iterating over instructions and looking at the
  // use count of the dest value. If it's zero, we can safely remove the
  // instruction. Once we do that, the use counts of any of the src ops may
  // go to zero and we recursively kill up the graph. This is kind of
  // nasty in that we walk randomly and scribble all over memory, but (I think)
  // it's better than doing passes over all instructions until we quiesce.
  // To prevent our iteration from getting all messed up we just replace
  // all removed ops with NOP and then do a single pass that removes them
  // all.

  bool any_removed = false;
  Block* block = builder->first_block();
  while (block) {
    Instr* i = block->instr_tail;
    while (i) {
      Instr* prev = i->prev;

      const OpcodeInfo* opcode = i->opcode;
      uint32_t signature = opcode->signature;
      if (!(opcode->flags & OPCODE_FLAG_VOLATILE) &&
          i->dest && !i->dest->use_head) {
        // Has no uses and is not volatile. This instruction can die!
        MakeNopRecursive(i);
        any_removed = true;
      }

      i = prev;
    }

    block = block->next;
  }

  // Remove all nops.
  if (any_removed) {
    Block* block = builder->first_block();
    while (block) {
      Instr* i = block->instr_head;
      while (i) {
        Instr* next = i->next;
        if (i->opcode == &OPCODE_NOP_info) {
          // Nop - remove!
          i->Remove();
        }
        i = next;
      }
      block = block->next;
    }
  }

  return 0;
}

void DeadCodeEliminationPass::MakeNopRecursive(Instr* i) {
  i->opcode = &hir::OPCODE_NOP_info;
  i->dest->def = NULL;
  i->dest = NULL;

#define MAKE_NOP_SRC(n) \
  if (i->src##n##_use) { \
    Value::Use* use = i->src##n##_use; \
    Value* value = i->src##n##.value; \
    i->src##n##_use = NULL; \
    i->src##n##.value = NULL; \
    value->RemoveUse(use); \
    if (!value->use_head) { \
      /* Value is now unused, so recursively kill it. */ \
      if (value->def && value->def != i) { \
        MakeNopRecursive(value->def); \
      } \
    } \
  }
  MAKE_NOP_SRC(1);
  MAKE_NOP_SRC(2);
  MAKE_NOP_SRC(3);
}
