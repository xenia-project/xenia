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

namespace alloy {
namespace compiler {
namespace passes {

// TODO(benvanik): remove when enums redefined.
using namespace alloy::hir;

using alloy::hir::HIRBuilder;

FinalizationPass::FinalizationPass() : CompilerPass() {}

FinalizationPass::~FinalizationPass() {}

int FinalizationPass::Run(HIRBuilder* builder) {
  SCOPE_profile_cpu_f("alloy");

  // Process the HIR and prepare it for lowering.
  // After this is done the HIR should be ready for emitting.

  auto arena = builder->arena();

  uint32_t block_ordinal = 0;
  auto block = builder->first_block();
  while (block) {
    block->ordinal = block_ordinal++;

    // Ensure all labels have names.
    auto label = block->label_head;
    while (label) {
      if (!label->name) {
        const size_t label_len = 6 + 4 + 1;
        char* name = (char*)arena->Alloc(label_len);
        xesnprintfa(name, label_len, "_label%d", label->id);
        label->name = name;
      }
      label = label->next;
    }

    // Remove unneeded jumps.
    auto tail = block->instr_tail;
    if (tail && tail->opcode == &OPCODE_BRANCH_info) {
      // Jump. Check target.
      auto target = tail->src1.label;
      if (target->block == block->next) {
        // Jumping to subsequent block. Remove.
        tail->Remove();
      }
    }

    block = block->next;
  }

  return 0;
}

}  // namespace passes
}  // namespace compiler
}  // namespace alloy
