/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/spirv/passes/control_flow_simplification_pass.h"

namespace xe {
namespace gpu {
namespace spirv {

ControlFlowSimplificationPass::ControlFlowSimplificationPass() {}

bool ControlFlowSimplificationPass::Run(spv::Module* module) {
  for (auto function : module->getFunctions()) {
    // Walk through the blocks in the function and merge any blocks which are
    // unconditionally dominated.
    for (auto it = function->getBlocks().end() - 1;
         it != function->getBlocks().begin();) {
      auto block = *it;
      if (!block->isUnreachable() && block->getPredecessors().size() == 1) {
        auto prev_block = block->getPredecessors()[0];
        auto last_instr =
            prev_block->getInstruction(prev_block->getInstructionCount() - 1);
        if (last_instr->getOpCode() == spv::Op::OpBranch) {
          if (prev_block->getSuccessors().size() == 1 &&
              prev_block->getSuccessors()[0] == block) {
            // We're dominated by this block. Merge into it.
            prev_block->merge(block);
            block->setUnreachable();
          }
        }
      }

      --it;
    }
  }

  return true;
}

}  // namespace spirv
}  // namespace gpu
}  // namespace xe
