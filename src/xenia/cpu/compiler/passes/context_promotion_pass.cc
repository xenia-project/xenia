/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/compiler/passes/context_promotion_pass.h"

#include "xenia/apu/apu_flags.h"
#include "xenia/base/cvar.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/compiler/compiler.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"

DECLARE_bool(debug);

DEFINE_bool(store_all_context_values, false,
            "Don't strip dead context stores to aid in debugging.", "CPU");

DEFINE_bool(full_optimization_even_with_debug, false,
            "For developer use to analyze the quality of the generated code, "
            "not intended for actual debugging of the code",
            "CPU");

namespace xe {
namespace cpu {
namespace compiler {
namespace passes {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::Block;
using xe::cpu::hir::HIRBuilder;
using xe::cpu::hir::Instr;
using xe::cpu::hir::Value;

ContextPromotionPass::ContextPromotionPass() : CompilerPass() {}

ContextPromotionPass::~ContextPromotionPass() {}

bool ContextPromotionPass::Initialize(Compiler* compiler) {
  if (!CompilerPass::Initialize(compiler)) {
    return false;
  }

  // This is a terrible implementation.
  context_values_.resize(sizeof(ppc::PPCContext));
  context_validity_.resize(static_cast<uint32_t>(sizeof(ppc::PPCContext)));

  return true;
}

bool ContextPromotionPass::Run(HIRBuilder* builder) {
  // Like mem2reg, but because context memory is unaliasable it's easier to
  // check and convert LoadContext/StoreContext into value operations.
  // Example of load->value promotion:
  //   v0 = load_context +100
  //   store_context +200, v0
  //   v1 = load_context +100  <-- replace with v1 = v0
  //   store_context +200, v1
  //
  // It'd be possible in this stage to also remove redundant context stores:
  // Example of dead store elimination:
  //   store_context +100, v0  <-- removed due to following store
  //   store_context +100, v1
  // This is more generally done by DSE, however if it could be done here
  // instead as it may be faster (at least on the block-level).

  // Promote loads to values.
  // Process each block independently, for now.
  auto block = builder->first_block();
  while (block) {
    PromoteBlock(block);
    block = block->next;
  }

  // Remove all dead stores.
  // This will break debugging as we can't recover this information when
  // trying to extract stack traces/register values, so we don't do that.
  if (cvars::full_optimization_even_with_debug ||
      (!cvars::debug && !cvars::store_all_context_values)) {
    block = builder->first_block();
    while (block) {
      RemoveDeadStoresBlock(block);
      block = block->next;
    }
  }

  return true;
}

void ContextPromotionPass::PromoteBlock(Block* block) {
  auto& validity = context_validity_;
  validity.reset();

  Instr* i = block->instr_head;
  while (i) {
    auto next = i->next;
    if (i->opcode->flags & OPCODE_FLAG_VOLATILE) {
      // Volatile instruction - requires all context values be flushed.
      validity.reset();
    } else if (i->opcode == &OPCODE_LOAD_CONTEXT_info) {
      size_t offset = i->src1.offset;
      if (validity.test(static_cast<uint32_t>(offset))) {
        // Legit previous value, reuse.
        Value* previous_value = context_values_[offset];
        i->opcode = &hir::OPCODE_ASSIGN_info;
        i->set_src1(previous_value);
      } else {
        // Store the loaded value into the table.
        context_values_[offset] = i->dest;
        validity.set(static_cast<uint32_t>(offset));
      }
    } else if (i->opcode == &OPCODE_STORE_CONTEXT_info) {
      size_t offset = i->src1.offset;
      Value* value = i->src2.value;
      // Store value into the table for later.
      context_values_[offset] = value;
      validity.set(static_cast<uint32_t>(offset));
    }
    i = next;
  }
}

void ContextPromotionPass::RemoveDeadStoresBlock(Block* block) {
  auto& validity = context_validity_;
  validity.reset();

  // Walk backwards and mark offsets that are written to.
  // If the offset was written to earlier, ignore the store.
  Instr* i = block->instr_tail;
  while (i) {
    Instr* prev = i->prev;
    if (i->opcode->flags & (OPCODE_FLAG_VOLATILE | OPCODE_FLAG_BRANCH)) {
      // Volatile instruction - requires all context values be flushed.
      validity.reset();
    } else if (i->opcode == &OPCODE_STORE_CONTEXT_info) {
      size_t offset = i->src1.offset;
      if (!validity.test(static_cast<uint32_t>(offset))) {
        // Offset not yet written, mark and continue.
        validity.set(static_cast<uint32_t>(offset));
      } else {
        // Already written to. Remove this store.
        i->UnlinkAndNOP();
      }
    }
    i = prev;
  }
}

}  // namespace passes
}  // namespace compiler
}  // namespace cpu
}  // namespace xe
