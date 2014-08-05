/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_FRONTEND_PPC_PPC_HIR_BUILDER_H_
#define ALLOY_FRONTEND_PPC_PPC_HIR_BUILDER_H_

#include <alloy/core.h>
#include <alloy/string_buffer.h>
#include <alloy/hir/hir_builder.h>
#include <alloy/runtime/function.h>
#include <alloy/runtime/symbol_info.h>

namespace alloy {
namespace frontend {
namespace ppc {

class PPCFrontend;

class PPCHIRBuilder : public hir::HIRBuilder {
  using Instr = alloy::hir::Instr;
  using Label = alloy::hir::Label;
  using Value = alloy::hir::Value;

 public:
  PPCHIRBuilder(PPCFrontend* frontend);
  virtual ~PPCHIRBuilder();

  virtual void Reset();

  int Emit(runtime::FunctionInfo* symbol_info, bool with_debug_info);

  runtime::FunctionInfo* symbol_info() const { return symbol_info_; }
  runtime::FunctionInfo* LookupFunction(uint64_t address);
  Label* LookupLabel(uint64_t address);

  Value* LoadLR();
  void StoreLR(Value* value);
  Value* LoadCTR();
  void StoreCTR(Value* value);
  Value* LoadCR(uint32_t n);
  Value* LoadCRField(uint32_t n, uint32_t bit);
  void StoreCR(uint32_t n, Value* value);
  void StoreCRField(uint32_t n, uint32_t bit, Value* value);
  void UpdateCR(uint32_t n, Value* lhs, bool is_signed = true);
  void UpdateCR(uint32_t n, Value* lhs, Value* rhs, bool is_signed = true);
  void UpdateCR6(Value* src_value);
  Value* LoadFPSCR();
  void StoreFPSCR(Value* value);
  Value* LoadXER();
  void StoreXER(Value* value);
  // void UpdateXERWithOverflow();
  // void UpdateXERWithOverflowAndCarry();
  // void StoreOV(Value* value);
  Value* LoadCA();
  void StoreCA(Value* value);
  Value* LoadSAT();
  void StoreSAT(Value* value);

  Value* LoadGPR(uint32_t reg);
  void StoreGPR(uint32_t reg, Value* value);
  Value* LoadFPR(uint32_t reg);
  void StoreFPR(uint32_t reg, Value* value);
  Value* LoadVR(uint32_t reg);
  void StoreVR(uint32_t reg, Value* value);

  Value* LoadAcquire(Value* address, hir::TypeName type,
                     uint32_t load_flags = 0);
  Value* StoreRelease(Value* address, Value* value, uint32_t store_flags = 0);

 private:
  void AnnotateLabel(uint64_t address, Label* label);

 private:
  PPCFrontend* frontend_;

  // Reset whenever needed:
  StringBuffer comment_buffer_;

  // Reset each Emit:
  bool with_debug_info_;
  runtime::FunctionInfo* symbol_info_;
  uint64_t start_address_;
  uint64_t instr_count_;
  Instr** instr_offset_list_;
  Label** label_list_;
};

}  // namespace ppc
}  // namespace frontend
}  // namespace alloy

#endif  // ALLOY_FRONTEND_PPC_PPC_HIR_BUILDER_H_
