/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_FRONTEND_PPC_HIR_BUILDER_H_
#define XENIA_FRONTEND_PPC_HIR_BUILDER_H_

#include "xenia/base/string_buffer.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/symbol_info.h"

namespace xe {
namespace cpu {
namespace frontend {

class PPCFrontend;

class PPCHIRBuilder : public hir::HIRBuilder {
  using Instr = xe::cpu::hir::Instr;
  using Label = xe::cpu::hir::Label;
  using Value = xe::cpu::hir::Value;

 public:
  PPCHIRBuilder(PPCFrontend* frontend);
  virtual ~PPCHIRBuilder();

  virtual void Reset();

  enum EmitFlags {
    // Emit comment nodes.
    EMIT_DEBUG_COMMENTS = 1 << 0,
  };
  bool Emit(FunctionInfo* symbol_info, uint32_t flags);

  FunctionInfo* symbol_info() const { return symbol_info_; }
  FunctionInfo* LookupFunction(uint32_t address);
  Label* LookupLabel(uint32_t address);

  Value* LoadLR();
  void StoreLR(Value* value);
  Value* LoadCTR();
  void StoreCTR(Value* value);
  Value* LoadCR();
  Value* LoadCR(uint32_t n);
  Value* LoadCRField(uint32_t n, uint32_t bit);
  void StoreCR(Value* value);
  void StoreCR(uint32_t n, Value* value);
  void StoreCRField(uint32_t n, uint32_t bit, Value* value);
  void UpdateCR(uint32_t n, Value* lhs, bool is_signed = true);
  void UpdateCR(uint32_t n, Value* lhs, Value* rhs, bool is_signed = true);
  void UpdateCR6(Value* src_value);
  Value* LoadMSR();
  void StoreMSR(Value* value);
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
  void AnnotateLabel(uint32_t address, Label* label);

 private:
  PPCFrontend* frontend_;

  // Reset whenever needed:
  StringBuffer comment_buffer_;

  // Reset each Emit:
  bool with_debug_info_;
  FunctionInfo* symbol_info_;
  uint64_t start_address_;
  uint64_t instr_count_;
  Instr** instr_offset_list_;
  Label** label_list_;

  // Reset each instruction.
  struct {
    uint32_t dest_count;
    struct {
      uint8_t reg;
      Value* value;
    } dests[4];
  } trace_info_;
};

}  // namespace frontend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_FRONTEND_PPC_HIR_BUILDER_H_
