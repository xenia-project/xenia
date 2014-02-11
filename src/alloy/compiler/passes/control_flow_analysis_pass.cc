/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/compiler/passes/control_flow_analysis_pass.h>

#include <alloy/backend/backend.h>
#include <alloy/compiler/compiler.h>
#include <alloy/runtime/runtime.h>

#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#include <llvm/ADT/BitVector.h>
#pragma warning(pop)

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::compiler;
using namespace alloy::compiler::passes;
using namespace alloy::frontend;
using namespace alloy::hir;
using namespace alloy::runtime;


ControlFlowAnalysisPass::ControlFlowAnalysisPass() :
    CompilerPass() {
}

ControlFlowAnalysisPass::~ControlFlowAnalysisPass() {
}

int ControlFlowAnalysisPass::Run(HIRBuilder* builder) {
  // TODO(benvanik): reset edges for all blocks? Needed to be re-runnable.

  // Add edges.
  auto block = builder->first_block();
  while (block) {
    auto instr = block->instr_tail;
    while (instr) {
      if (instr->opcode->flags & OPCODE_FLAG_BRANCH) {
        if (instr->opcode == &OPCODE_BRANCH_info) {
          auto label = instr->src1.label;
          builder->AddEdge(block, label->block, Edge::UNCONDITIONAL);
          break;
        } else if (instr->opcode == &OPCODE_BRANCH_TRUE_info ||
                   instr->opcode == &OPCODE_BRANCH_FALSE_info) {
          auto label = instr->src2.label;
          builder->AddEdge(block, label->block, 0);
          break;
        }
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

  return 0;
}
