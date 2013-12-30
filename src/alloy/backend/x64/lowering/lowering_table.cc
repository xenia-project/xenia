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
    auto fn = lookup_[n];
    while (fn) {
      auto next = fn->next;
      delete fn;
      fn = next;
    }
  }
}

int LoweringTable::Initialize() {
  RegisterSequences(this);
  return 0;
}

void LoweringTable::AddSequence(hir::Opcode starting_opcode, FnWrapper* fn) {
  auto existing_fn = lookup_[starting_opcode];
  fn->next = existing_fn;
  lookup_[starting_opcode] = fn;
}

int LoweringTable::Process(lir::LIRBuilder* builder) {
  /*LIRInstr* instr = 0;
  auto fn = lookup_[instr->opcode->num];
  while (fn) {
    if ((*fn)(builder, instr)) {
      return true;
    }
    fn = fn->next;
  }*/
  return 0;
}