/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/compiler/passes/finalization_pass.h>

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


FinalizationPass::FinalizationPass() :
    CompilerPass() {
}

FinalizationPass::~FinalizationPass() {
}

int FinalizationPass::Run(HIRBuilder* builder) {
  // Process the HIR and prepare it for lowering.
  // After this is done the HIR should be ready for emitting.

  auto arena = builder->arena();

  auto block = builder->first_block();
  while (block) {
    // Ensure all labels have names.
    auto label = block->label_head;
    while (label) {
      if (!label->name) {
        char* name = (char*)arena->Alloc(6 + 4 + 1);
        xestrcpya(name, 6 + 1, "_label");
        char* part = _itoa(label->id, name + 6, 10);
        label->name = name;
      }
      label = label->next;
    }

    // ? remove useless jumps?

    // Renumber all instructions to make liveness tracking easier.
    uint32_t n = 0;
    auto instr = block->instr_head;
    while (instr) {
      instr->ordinal = n++;
      instr = instr->next;
    }

    block = block->next;
  }

  return 0;
}
