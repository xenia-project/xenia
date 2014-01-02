/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/lowering/lowering_table.h>

#include <alloy/backend/x64/lowering/lowering_sequences.h>

using namespace alloy;
using namespace alloy::backend::x64;
using namespace alloy::backend::x64::lowering;


LoweringTable::LoweringTable(X64Backend* backend) :
    backend_(backend) {
  xe_zero_struct(lookup_, sizeof(lookup_));
}

LoweringTable::~LoweringTable() {
  for (size_t n = 0; n < XECOUNT(lookup_); n++) {
    auto entry = lookup_[n];
    while (entry) {
      auto next = entry->next;
      delete entry;
      entry = next;
    }
  }
}

int LoweringTable::Initialize() {
  RegisterSequences(this);
  return 0;
}

void LoweringTable::AddSequence(hir::Opcode starting_opcode, sequence_fn_t fn) {
  auto existing_entry = lookup_[starting_opcode];
  auto new_entry = new sequence_fn_entry_t();
  new_entry->fn = fn;
  new_entry->next = existing_entry;
  lookup_[starting_opcode] = new_entry;
}

int LoweringTable::Process(
    hir::HIRBuilder* hir_builder, lir::LIRBuilder* lir_builder) {
  lir_builder->EndBlock();

  // Translate all labels ahead of time.
  // We stash them on tags to make things easier later on.
  auto hir_block = hir_builder->first_block();
  while (hir_block) {
    auto hir_label = hir_block->label_head;
    while (hir_label) {
      // TODO(benvanik): copy name to LIR label.
      hir_label->tag = lir_builder->NewLabel();
      hir_label = hir_label->next;
    }
    hir_block = hir_block->next;
  }

  // Process each block.
  hir_block = hir_builder->first_block();
  while (hir_block) {
    // Force a new block.
    lir_builder->AppendBlock();

    // Mark labels.
    auto hir_label = hir_block->label_head;
    while (hir_label) {
      auto lir_label = (lir::LIRLabel*)hir_label->tag;
      lir_builder->MarkLabel(lir_label);
      hir_label = hir_label->next;
    }

    // Process instructions.
    auto hir_instr = hir_block->instr_head;
    while (hir_instr) {
      bool processed = false;
      auto entry = lookup_[hir_instr->opcode->num];
      while (entry) {
        if ((*entry->fn)(*lir_builder, hir_instr)) {
          processed = true;
          break;
        }
        entry = entry->next;
      }
      if (!processed) {
        // No sequence found!
        XELOGE("Unable to process HIR opcode %s", hir_instr->opcode->name);
        return 1;
        hir_instr = hir_instr->next;
      }
    }

    lir_builder->EndBlock();
    hir_block = hir_block->next;
  }

  return 0;
}