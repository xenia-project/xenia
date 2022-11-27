/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_PPC_HIR_BUILDER_H_
#define XENIA_CPU_PPC_PPC_HIR_BUILDER_H_

#include "xenia/base/string_buffer.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/hir/hir_builder.h"

namespace xe {
namespace cpu {
namespace ppc {

struct PPCBuiltins;
class PPCFrontend;

class PPCHIRBuilder : public hir::HIRBuilder {
  using Instr = xe::cpu::hir::Instr;
  using Label = xe::cpu::hir::Label;
  using Value = xe::cpu::hir::Value;

 public:
  explicit PPCHIRBuilder(PPCFrontend* frontend);
  ~PPCHIRBuilder() override;

  PPCBuiltins* builtins() const;

  void Reset() override;

  enum EmitFlags {
    // Emit comment nodes.
    EMIT_DEBUG_COMMENTS = 1 << 0,
  };
  bool Emit(GuestFunction* function, uint32_t flags);

  GuestFunction* function() const { return function_; }
  Function* LookupFunction(uint32_t address);
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
  Value* LoadFPSCR();
  void StoreFPSCR(Value* value);
  void UpdateFPSCR(Value* result, bool update_cr1);
  void CopyFPSCRToCR1();
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

  void StoreReserved(Value* val);
  Value* LoadReserved();
  //calls original impl in hirbuilder, but also records the is_return_site bit into flags in the guestmodule
  void SetReturnAddress(Value* value);
 private:
  void MaybeBreakOnInstruction(uint32_t address);
  void AnnotateLabel(uint32_t address, Label* label);

  PPCFrontend* frontend_;

  // Reset whenever needed:
  StringBuffer comment_buffer_;

  // Reset each Emit:
  bool with_debug_info_;
  GuestFunction* function_;
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

}  // namespace ppc
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PPC_PPC_HIR_BUILDER_H_
