/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/control_flow_analysis_pass.h"

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

ControlFlowAnalysisPass::ControlFlowAnalysisPass() : CompilerPass() {}

ControlFlowAnalysisPass::~ControlFlowAnalysisPass() {}

bool ControlFlowAnalysisPass::Run(HIRBuilder* builder) {
  // Reset edges for all blocks. Needed to be re-runnable.
  // Note that this wastes a bunch of arena memory, so we shouldn't
  // re-run too often.
  auto block = builder->first_block();
  while (block) {
    block->incoming_edge_head = nullptr;
    block->outgoing_edge_head = nullptr;
    block = block->next;
  }

  // Add edges.
  block = builder->first_block();
  while (block) {
    auto instr = block->instr_tail;
    while (instr) {
      if ((instr->opcode->flags & OPCODE_FLAG_BRANCH) == 0) {
        break;
      }
      if (instr->opcode == &OPCODE_BRANCH_info) {
        auto label = instr->src1.label;
        builder->AddEdge(block, label->block, Edge::UNCONDITIONAL);
      } else if (instr->opcode == &OPCODE_BRANCH_TRUE_info ||
                 instr->opcode == &OPCODE_BRANCH_FALSE_info) {
        auto label = instr->src2.label;
        builder->AddEdge(block, label->block, 0);
      }
      instr = instr->prev;
    }
    block = block->next;
  }

  // Mark dominators.
  block = builder->first_block();
  while (block) {
    if (block->incoming_edge_head &&
        !block->incoming_edge_head->incoming_next) {
      block->incoming_edge_head->flags |= Edge::DOMINATES;
    }
    block = block->next;
  }

  return true;
}

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe
