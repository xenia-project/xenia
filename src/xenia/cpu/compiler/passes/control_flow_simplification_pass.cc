/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/control_flow_simplification_pass.h"

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

using xe::cpu::hir::Edge;
using xe::cpu::hir::HIRBuilder;

ControlFlowSimplificationPass::ControlFlowSimplificationPass()
    : CompilerPass() {}

ControlFlowSimplificationPass::~ControlFlowSimplificationPass() {}

bool ControlFlowSimplificationPass::Run(HIRBuilder* builder) {
  // Walk forwards and kill any unreachable blocks.
  // Do this before merging.
  auto block = builder->first_block();
  while (block) {
    auto next_block = block->next;
    if (!block->incoming_edge_head && block->prev) {
      // Block is in the interior and has no incoming edges - kill it.
      builder->RemoveBlock(block);
    }
    block = next_block;
  }

  // Walk backwards and merge blocks if possible.
  bool merged_any = false;
  block = builder->last_block();
  while (block) {
    auto prev_block = block->prev;
    const uint32_t expected = Edge::DOMINATES | Edge::UNCONDITIONAL;
    if (block->incoming_edge_head &&
        (block->incoming_edge_head->flags & expected) == expected) {
      // Dominated by the incoming block.
      // If that block comes immediately before us then we can merge the
      // two blocks (assuming it's not a volatile instruction like Trap).
      if (block->prev == block->incoming_edge_head->src &&
          block->prev->instr_tail &&
          !(block->prev->instr_tail->opcode->flags & OPCODE_FLAG_VOLATILE)) {
        builder->MergeAdjacentBlocks(block->prev, block);
        merged_any = true;
      }
    }
    block = prev_block;
  }

  return true;
}

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe
