/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/compiler/passes/control_flow_simplification_pass.h>

#include <alloy/backend/backend.h>
#include <alloy/compiler/compiler.h>
#include <alloy/runtime/runtime.h>

namespace alloy {
namespace compiler {
namespace passes {

// TODO(benvanik): remove when enums redefined.
using namespace alloy::hir;

using alloy::hir::Edge;
using alloy::hir::HIRBuilder;

ControlFlowSimplificationPass::ControlFlowSimplificationPass()
    : CompilerPass() {}

ControlFlowSimplificationPass::~ControlFlowSimplificationPass() {}

int ControlFlowSimplificationPass::Run(HIRBuilder* builder) {
  SCOPE_profile_cpu_f("alloy");

  // Walk backwards and merge blocks if possible.
  bool merged_any = false;
  auto block = builder->last_block();
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

  return 0;
}

}  // namespace passes
}  // namespace compiler
}  // namespace alloy
