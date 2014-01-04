/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/optimizer/passes/reachability_pass.h>

using namespace alloy;
using namespace alloy::backend::x64::lir;
using namespace alloy::backend::x64::optimizer;
using namespace alloy::backend::x64::optimizer::passes;


ReachabilityPass::ReachabilityPass() :
    OptimizerPass() {
}

ReachabilityPass::~ReachabilityPass() {
}

int ReachabilityPass::Run(LIRBuilder* builder) {
  // Run through blocks. If blocks have no incoming edges remove them.
  // Afterwards, remove any unneeded jumps (jumping to the next block).

  // TODO(benvanik): dead block removal.

  // Remove unneeded jumps.
  auto block = builder->first_block();
  while (block) {
    auto tail = block->instr_tail;
    if (tail && tail->opcode == &LIR_OPCODE_JUMP_info) {
      // Jump. Check target.
      if (tail->arg0_type() == LIROperandType::LABEL) {
        auto target = *tail->arg0<LIRLabel*>();
        if (target->block == block->next) {
          // Jumping to subsequent block. Remove.
          tail->Remove();
        }
      }
    }
    block = block->next;
  }

  return 0;
}
