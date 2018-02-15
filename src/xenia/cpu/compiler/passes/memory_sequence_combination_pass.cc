/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/memory_sequence_combination_pass.h"

#include "xenia/base/profiling.h"

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::HIRBuilder;
using xe::cpu::hir::Instr;
using xe::cpu::hir::Value;

MemorySequenceCombinationPass::MemorySequenceCombinationPass()
    : CompilerPass() {}

MemorySequenceCombinationPass::~MemorySequenceCombinationPass() = default;

bool MemorySequenceCombinationPass::Run(HIRBuilder* builder) {
  // Run over all loads and stores and see if we can collapse sequences into the
  // fat opcodes. See the respective utility functions for examples.
  auto block = builder->first_block();
  while (block) {
    auto i = block->instr_head;
    while (i) {
      if (i->opcode == &OPCODE_LOAD_info ||
          i->opcode == &OPCODE_LOAD_OFFSET_info) {
        CombineLoadSequence(i);
      } else if (i->opcode == &OPCODE_STORE_info ||
                 i->opcode == &OPCODE_STORE_OFFSET_info) {
        CombineStoreSequence(i);
      }
      i = i->next;
    }
    block = block->next;
  }
  return true;
}

void MemorySequenceCombinationPass::CombineLoadSequence(Instr* i) {
  // Load with swap:
  //   v1.i32 = load v0
  //   v2.i32 = byte_swap v1.i32
  // becomes:
  //   v1.i32 = load v0, [swap]
  //
  // Load with swap and extend:
  //   v1.i32 = load v0
  //   v2.i32 = byte_swap v1.i32
  //   v3.i64 = zero_extend v2.i32
  // becomes:
  //   v1.i64 = load_convert v0, [swap|i32->i64,zero]

  if (!i->dest->use_head) {
    // No uses of the load result - ignore. Will be killed by DCE.
    return;
  }

  // Ensure all uses of the load result are BYTE_SWAP - if it's mixed we
  // shouldn't transform as we'd have to introduce new swaps!
  auto use = i->dest->use_head;
  while (use) {
    if (use->instr->opcode != &OPCODE_BYTE_SWAP_info) {
      // Not a swap.
      return;
    }
    // TODO(benvanik): allow uses by STORE (we can make that swap).
    use = use->next;
  }

  // Merge byte swap into load.
  // Note that we may have already been a swapped operation - this inverts that.
  i->flags ^= LoadStoreFlags::LOAD_STORE_BYTE_SWAP;

  // Replace use of byte swap value with loaded value.
  // It's byte_swap vN -> assign vN, so not much to do.
  use = i->dest->use_head;
  while (use) {
    auto next_use = use->next;
    use->instr->opcode = &OPCODE_ASSIGN_info;
    use->instr->flags = 0;
    use = next_use;
  }

  // TODO(benvanik): merge in extend/truncate.
}

void MemorySequenceCombinationPass::CombineStoreSequence(Instr* i) {
  // Store with swap:
  //   v1.i32 = ...
  //   v2.i32 = byte_swap v1.i32
  //   store v0, v2.i32
  // becomes:
  //   store v0, v1.i32, [swap]
  //
  // Store with truncate and swap:
  //   v1.i64 = ...
  //   v2.i32 = truncate v1.i64
  //   v3.i32 = byte_swap v2.i32
  //   store v0, v3.i32
  // becomes:
  //   store_convert v0, v1.i64, [swap|i64->i32,trunc]

  auto src = i->src2.value;
  if (i->opcode == &OPCODE_STORE_OFFSET_info) {
    src = i->src3.value;
  }

  if (src->IsConstant()) {
    // Constant value write - ignore.
    return;
  }

  // Find source and ensure it is a byte swap.
  auto def = src->def;
  while (def && def->opcode == &OPCODE_ASSIGN_info) {
    // Skip asignments.
    def = def->src1.value->def;
  }
  if (!def || def->opcode != &OPCODE_BYTE_SWAP_info) {
    // Not a swap/not defined?
    return;
  }

  // Merge byte swap into store.
  // Note that we may have already been a swapped operation - this inverts
  // that.
  i->flags ^= LoadStoreFlags::LOAD_STORE_BYTE_SWAP;

  // Pull the original value (from before the byte swap).
  // The byte swap itself will go away in DCE.
  if (i->opcode == &OPCODE_STORE_info) {
    i->set_src2(def->src1.value);
  } else if (i->opcode == &OPCODE_STORE_OFFSET_info) {
    i->set_src3(def->src1.value);
  }

  // TODO(benvanik): extend/truncate.
}

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe
