/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/compiler/passes/register_allocation_pass.h>

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


RegisterAllocationPass::RegisterAllocationPass(Backend* backend) :
    CompilerPass() {
  // TODO(benvanik): query backend info (register layout, etc).
}

RegisterAllocationPass::~RegisterAllocationPass() {
}

int RegisterAllocationPass::Run(HIRBuilder* builder) {
  // Run through each block and give each dest value a register.
  // This pass is currently really dumb, and will over spill and other badness.

  auto block = builder->first_block();
  while (block) {
    if (ProcessBlock(block)) {
      return 1;
    }
    block = block->next;
  }

  return 0;
}

int RegisterAllocationPass::ProcessBlock(Block* block) {
  //
  return 0;
}
