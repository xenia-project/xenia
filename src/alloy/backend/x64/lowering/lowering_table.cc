/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/lowering/lowering_table.h>

#include <alloy/backend/x64/x64_emitter.h>
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

int LoweringTable::ProcessBlock(X64Emitter& e, hir::Block* block) {
  // Process instructions.
  auto instr = block->instr_head;
  while (instr) {
    bool processed = false;
    auto entry = lookup_[instr->opcode->num];
    while (entry) {
      if ((*entry->fn)(e, instr)) {
        processed = true;
        break;
      }
      entry = entry->next;
    }
    if (!processed) {
      // No sequence found!
      XELOGE("Unable to process HIR opcode %s", instr->opcode->name);
      return 1;
      instr = e.Advance(instr);
    }
  }

  return 0;
}