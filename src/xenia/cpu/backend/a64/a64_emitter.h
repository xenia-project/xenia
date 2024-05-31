/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_EMITTER_H_
#define XENIA_CPU_BACKEND_A64_A64_EMITTER_H_

#include <unordered_map>
#include <vector>

#include "xenia/base/arena.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/function_trace_data.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/hir/instr.h"
#include "xenia/cpu/hir/value.h"
#include "xenia/memory.h"

#include "oaknut/code_block.hpp"
#include "oaknut/oaknut.hpp"

namespace xe {
namespace cpu {
class Processor;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

class A64Backend;
class A64CodeCache;

struct EmitFunctionInfo;

enum RegisterFlags {
  REG_DEST = (1 << 0),
  REG_ABCD = (1 << 1),
};

enum VConst {
  VZero = 0,
  VOnePD,
  VNegativeOne,
  VFFFF,
  VMaskX16Y16,
  VFlipX16Y16,
  VFixX16Y16,
  VNormalizeX16Y16,
  V0001,
  V3301,
  V3331,
  V3333,
  VSignMaskPS,
  VSignMaskPD,
  VAbsMaskPS,
  VAbsMaskPD,
  VByteSwapMask,
  VByteOrderMask,
  VPermuteControl15,
  VPermuteByteMask,
  VPackD3DCOLORSat,
  VPackD3DCOLOR,
  VUnpackD3DCOLOR,
  VPackFLOAT16_2,
  VUnpackFLOAT16_2,
  VPackFLOAT16_4,
  VUnpackFLOAT16_4,
  VPackSHORT_Min,
  VPackSHORT_Max,
  VPackSHORT_2,
  VPackSHORT_4,
  VUnpackSHORT_2,
  VUnpackSHORT_4,
  VUnpackSHORT_Overflow,
  VPackUINT_2101010_MinUnpacked,
  VPackUINT_2101010_MaxUnpacked,
  VPackUINT_2101010_MaskUnpacked,
  VPackUINT_2101010_MaskPacked,
  VPackUINT_2101010_Shift,
  VUnpackUINT_2101010_Overflow,
  VPackULONG_4202020_MinUnpacked,
  VPackULONG_4202020_MaxUnpacked,
  VPackULONG_4202020_MaskUnpacked,
  VPackULONG_4202020_PermuteXZ,
  VPackULONG_4202020_PermuteYW,
  VUnpackULONG_4202020_Permute,
  VUnpackULONG_4202020_Overflow,
  VOneOver255,
  VMaskEvenPI16,
  VShiftMaskEvenPI16,
  VShiftMaskPS,
  VShiftByteMask,
  VSwapWordMask,
  VUnsignedDwordMax,
  V255,
  VPI32,
  VSignMaskI8,
  VSignMaskI16,
  VSignMaskI32,
  VSignMaskF32,
  VShortMinPS,
  VShortMaxPS,
  VIntMin,
  VIntMax,
  VIntMaxPD,
  VPosIntMinPS,
  VQNaN,
  VInt127,
  V2To32,
};

enum A64EmitterFeatureFlags {
  kA64EmitLSE = 1 << 0,
  kA64EmitF16C = 1 << 1,
};

class A64Emitter : public oaknut::CodeBlock, public oaknut::CodeGenerator {
 public:
  A64Emitter(A64Backend* backend);
  virtual ~A64Emitter();

  Processor* processor() const { return processor_; }
  A64Backend* backend() const { return backend_; }

  static uintptr_t PlaceConstData();
  static void FreeConstData(uintptr_t data);

  bool Emit(GuestFunction* function, hir::HIRBuilder* builder,
            uint32_t debug_info_flags, FunctionDebugInfo* debug_info,
            void** out_code_address, size_t* out_code_size,
            std::vector<SourceMapEntry>* out_source_map);

 public:
  // Reserved:  XSP, X27, X28
  // Scratch:   X1-X15, X30 | V0-v7 and V16-V31
  //            V0-2
  // Available: X19-X26
  //            V4-V15 (save to get V3)
  static const size_t GPR_COUNT = 8;
  static const size_t FPR_COUNT = 8;

  static void SetupReg(const hir::Value* v, oaknut::WReg& r) {
    const auto idx = gpr_reg_map_[v->reg.index];
    r = oaknut::WReg(idx);
  }
  static void SetupReg(const hir::Value* v, oaknut::XReg& r) {
    const auto idx = gpr_reg_map_[v->reg.index];
    r = oaknut::XReg(idx);
  }
  static void SetupReg(const hir::Value* v, oaknut::SReg& r) {
    const auto idx = fpr_reg_map_[v->reg.index];
    r = oaknut::SReg(idx);
  }
  static void SetupReg(const hir::Value* v, oaknut::DReg& r) {
    const auto idx = fpr_reg_map_[v->reg.index];
    r = oaknut::DReg(idx);
  }
  static void SetupReg(const hir::Value* v, oaknut::QReg& r) {
    const auto idx = fpr_reg_map_[v->reg.index];
    r = oaknut::QReg(idx);
  }

  // Gets(and possibly create) an HIR label with the specified name
  oaknut::Label* lookup_label(const char* label_name) {
    return &label_lookup_[label_name];
  }

  oaknut::Label& epilog_label() { return *epilog_label_; }

  void MarkSourceOffset(const hir::Instr* i);

  void DebugBreak();
  void Trap(uint16_t trap_type = 0);
  void UnimplementedInstr(const hir::Instr* i);

  void Call(const hir::Instr* instr, GuestFunction* function);
  void CallIndirect(const hir::Instr* instr, const oaknut::XReg& reg);
  void CallExtern(const hir::Instr* instr, const Function* function);
  void CallNative(void* fn);
  void CallNative(uint64_t (*fn)(void* raw_context));
  void CallNative(uint64_t (*fn)(void* raw_context, uint64_t arg0));
  void CallNative(uint64_t (*fn)(void* raw_context, uint64_t arg0),
                  uint64_t arg0);
  void CallNativeSafe(void* fn);
  void SetReturnAddress(uint64_t value);

  static oaknut::XReg GetNativeParam(uint32_t param);

  static oaknut::XReg GetContextReg();
  static oaknut::XReg GetMembaseReg();
  void ReloadContext();
  void ReloadMembase();

  // Moves a 64bit immediate into memory.
  static bool ConstantFitsIn32Reg(uint64_t v);
  void MovMem64(const oaknut::XRegSp& addr, intptr_t offset, uint64_t v);

  std::byte* GetVConstPtr() const;
  std::byte* GetVConstPtr(VConst id) const;
  static constexpr uintptr_t GetVConstOffset(VConst id) {
    return sizeof(vec128_t) * id;
  }
  void LoadConstantV(oaknut::QReg dest, float v);
  void LoadConstantV(oaknut::QReg dest, double v);
  void LoadConstantV(oaknut::QReg dest, const vec128_t& v);

  // Returned addresses are relative to XSP
  uintptr_t StashV(int index, const oaknut::QReg& r);
  uintptr_t StashConstantV(int index, float v);
  uintptr_t StashConstantV(int index, double v);
  uintptr_t StashConstantV(int index, const vec128_t& v);

  bool IsFeatureEnabled(uint32_t feature_flag) const {
    return (feature_flags_ & feature_flag) == feature_flag;
  }

  FunctionDebugInfo* debug_info() const { return debug_info_; }

  size_t stack_size() const { return stack_size_; }

 protected:
  void* Emplace(const EmitFunctionInfo& func_info,
                GuestFunction* function = nullptr);
  bool Emit(hir::HIRBuilder* builder, EmitFunctionInfo& func_info);
  void EmitGetCurrentThreadId();
  void EmitTraceUserCallReturn();

 protected:
  Processor* processor_ = nullptr;
  A64Backend* backend_ = nullptr;
  A64CodeCache* code_cache_ = nullptr;
  uint32_t feature_flags_ = 0;

  oaknut::Label* epilog_label_ = nullptr;

  // Convert from plain-text label-names into oaknut-labels
  std::unordered_map<std::string, oaknut::Label> label_lookup_;

  hir::Instr* current_instr_ = nullptr;

  FunctionDebugInfo* debug_info_ = nullptr;
  uint32_t debug_info_flags_ = 0;
  FunctionTraceData* trace_data_ = nullptr;
  Arena source_map_arena_;

  size_t stack_size_ = 0;

  static const uint8_t gpr_reg_map_[GPR_COUNT];
  static const uint8_t fpr_reg_map_[FPR_COUNT];
};

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_EMITTER_H_
