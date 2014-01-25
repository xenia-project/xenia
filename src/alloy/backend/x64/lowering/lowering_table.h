/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_X64_LOWERING_LOWERING_TABLE_H_
#define ALLOY_BACKEND_X64_X64_LOWERING_LOWERING_TABLE_H_

#include <alloy/core.h>
#include <alloy/hir/hir_builder.h>


namespace alloy {
namespace backend {
namespace x64 {
class X64Backend;
class X64Emitter;
namespace lowering {


class LoweringTable {
public:
  LoweringTable(X64Backend* backend);
  ~LoweringTable();

  int Initialize();

  int ProcessBlock(X64Emitter& e, hir::Block* block);

public:
  typedef bool(*sequence_fn_t)(X64Emitter& e, hir::Instr*& instr);
  void AddSequence(hir::Opcode starting_opcode, sequence_fn_t fn);

private:
  class sequence_fn_entry_t {
  public:
    sequence_fn_t fn;
    sequence_fn_entry_t* next;
  };

  // NOTE: this class is shared by multiple threads and is not thread safe.
  // Do not modify anything after init.
  X64Backend* backend_;
  sequence_fn_entry_t* lookup_[hir::__OPCODE_MAX_VALUE];
};


}  // namespace lowering
}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_X64_LOWERING_LOWERING_TABLE_H_
