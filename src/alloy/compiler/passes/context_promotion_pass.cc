/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/compiler/passes/context_promotion_pass.h>

#include <gflags/gflags.h>

#include <alloy/compiler/compiler.h>
#include <alloy/runtime/runtime.h>

using namespace alloy;
using namespace alloy::compiler;
using namespace alloy::compiler::passes;
using namespace alloy::frontend;
using namespace alloy::hir;
using namespace alloy::runtime;


DEFINE_bool(store_all_context_values, false,
            "Don't strip dead context stores to aid in debugging.");


ContextPromotionPass::ContextPromotionPass() :
    context_values_size_(0), context_values_(0),
    CompilerPass() {
}

ContextPromotionPass::~ContextPromotionPass() {
  if (context_values_) {
    xe_free(context_values_);
  }
}

int ContextPromotionPass::Initialize(Compiler* compiler) {
  if (CompilerPass::Initialize(compiler)) {
    return 1;
  }

  // This is a terrible implementation.
  ContextInfo* context_info = runtime_->frontend()->context_info();
  context_values_size_ = context_info->size() * sizeof(Value*);
  context_values_ = (Value**)xe_calloc(context_values_size_);

  return 0;
}

int ContextPromotionPass::Run(HIRBuilder* builder) {
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
  Block* block = builder->first_block();
  while (block) {
    PromoteBlock(block);
    block = block->next;
  }

  // Remove all dead stores.
  if (!FLAGS_store_all_context_values) {
    block = builder->first_block();
    while (block) {
      RemoveDeadStoresBlock(block);
      block = block->next;
    }
  }

  return 0;
}

void ContextPromotionPass::PromoteBlock(Block* block) {
  // Clear the context values list.
  // TODO(benvanik): new data structure that isn't so stupid.
  //     Bitvector of validity, perhaps?
  xe_zero_struct(context_values_, context_values_size_);

  Instr* i = block->instr_head;
  while (i) {
    if (i->opcode == &OPCODE_LOAD_CONTEXT_info) {
      size_t offset = i->src1.offset;
      Value* previous_value = context_values_[offset];
      if (previous_value) {
        // Legit previous value, reuse.
        i->opcode = &hir::OPCODE_ASSIGN_info;
        i->set_src1(previous_value);
      } else {
        // Store the loaded value into the table.
        context_values_[offset] = i->dest;
      }
    } else if (i->opcode == &OPCODE_STORE_CONTEXT_info) {
      size_t offset = i->src1.offset;
      Value* value = i->src2.value;
      // Store value into the table for later.
      context_values_[offset] = value;
    }

    i = i->next;
  }
}

void ContextPromotionPass::RemoveDeadStoresBlock(Block* block) {
  // TODO(benvanik): use a bitvector.
  // To avoid clearing the structure, we use a token.
  Value* token = (Value*)block;

  // Walk backwards and mark offsets that are written to.
  // If the offset was written to earlier, ignore the store.
  Instr* i = block->instr_tail;
  while (i) {
    Instr* prev = i->prev;
    if (i->opcode == &OPCODE_STORE_CONTEXT_info) {
      size_t offset = i->src1.offset;
      if (context_values_[offset] != token) {
        // Mark offset as written to.
        context_values_[offset] = token;
      } else {
        // Already written to. Remove this store.
        i->Remove();
      }
    }
    i = prev;
  }
}
