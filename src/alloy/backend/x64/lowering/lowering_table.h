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
#include <alloy/backend/x64/lir/lir_builder.h>
#include <alloy/backend/x64/lir/lir_instr.h>
#include <alloy/hir/hir_builder.h>


namespace alloy {
namespace backend {
namespace x64 {
class X64Backend;
namespace lowering {


class LoweringTable {
public:
  LoweringTable(X64Backend* backend);
  ~LoweringTable();

  int Initialize();

  int Process(hir::HIRBuilder* hir_builder, lir::LIRBuilder* lir_builder);

public:
  class FnWrapper {
  public:
    FnWrapper() : next(0) {}
    virtual bool operator()(lir::LIRBuilder* builder, hir::Instr*& instr) const = 0;
    FnWrapper* next;
  };
  template<typename T>
  class TypedFnWrapper : public FnWrapper {
  public:
    TypedFnWrapper(T fn) : fn_(fn) {}
    virtual bool operator()(lir::LIRBuilder* builder, hir::Instr*& instr) const {
      return fn_(builder, instr);
    }
  private:
    T fn_;
  };

  void AddSequence(hir::Opcode starting_opcode, FnWrapper* fn);

private:
  X64Backend* backend_;
  FnWrapper* lookup_[hir::__OPCODE_MAX_VALUE];
};


}  // namespace lowering
}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_X64_LOWERING_LOWERING_TABLE_H_
