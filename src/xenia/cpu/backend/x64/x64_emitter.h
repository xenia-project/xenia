/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_X64_X64_EMITTER_H_
#define XENIA_CPU_BACKEND_X64_X64_EMITTER_H_

#include <vector>

#include "xenia/base/arena.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/function_trace_data.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/hir/instr.h"
#include "xenia/cpu/hir/value.h"
#include "xenia/memory.h"

// NOTE: must be included last as it expects windows.h to already be included.
#include "third_party/xbyak/xbyak/xbyak.h"
#include "third_party/xbyak/xbyak/xbyak_bin2hex.h"
#include "third_party/xbyak/xbyak/xbyak_util.h"

namespace xe {
namespace cpu {
class Processor;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

class X64Backend;
class X64CodeCache;

enum RegisterFlags {
  REG_DEST = (1 << 0),
  REG_ABCD = (1 << 1),
};

enum XmmConst {
  XMMZero = 0,
  XMMOne,
  XMMNegativeOne,
  XMMFFFF,
  XMMMaskX16Y16,
  XMMFlipX16Y16,
  XMMFixX16Y16,
  XMMNormalizeX16Y16,
  XMM0001,
  XMM3301,
  XMM3331,
  XMM3333,
  XMMSignMaskPS,
  XMMSignMaskPD,
  XMMAbsMaskPS,
  XMMAbsMaskPD,
  XMMByteSwapMask,
  XMMByteOrderMask,
  XMMPermuteControl15,
  XMMPermuteByteMask,
  XMMPackD3DCOLORSat,
  XMMPackD3DCOLOR,
  XMMUnpackD3DCOLOR,
  XMMPackFLOAT16_2,
  XMMUnpackFLOAT16_2,
  XMMPackFLOAT16_4,
  XMMUnpackFLOAT16_4,
  XMMPackSHORT_Min,
  XMMPackSHORT_Max,
  XMMPackSHORT_2,
  XMMPackSHORT_4,
  XMMUnpackSHORT_2,
  XMMUnpackSHORT_4,
  XMMUnpackSHORT_Overflow,
  XMMPackUINT_2101010_MinUnpacked,
  XMMPackUINT_2101010_MaxUnpacked,
  XMMPackUINT_2101010_MaskUnpacked,
  XMMPackUINT_2101010_MaskPacked,
  XMMPackUINT_2101010_Shift,
  XMMUnpackUINT_2101010_Overflow,
  XMMUnpackOverflowNaN,
  XMMOneOver255,
  XMMMaskEvenPI16,
  XMMShiftMaskEvenPI16,
  XMMShiftMaskPS,
  XMMShiftByteMask,
  XMMSwapWordMask,
  XMMUnsignedDwordMax,
  XMM255,
  XMMPI32,
  XMMSignMaskI8,
  XMMSignMaskI16,
  XMMSignMaskI32,
  XMMSignMaskF32,
  XMMShortMinPS,
  XMMShortMaxPS,
  XMMIntMin,
  XMMIntMax,
  XMMIntMaxPD,
  XMMPosIntMinPS,
};

// Unfortunately due to the design of xbyak we have to pass this to the ctor.
class XbyakAllocator : public Xbyak::Allocator {
 public:
  virtual bool useProtect() const { return false; }
};

enum X64EmitterFeatureFlags {
  kX64EmitAVX2 = 1 << 1,
  kX64EmitFMA = 1 << 2,
  kX64EmitLZCNT = 1 << 3,
  kX64EmitBMI2 = 1 << 4,
  kX64EmitF16C = 1 << 5,
  kX64EmitMovbe = 1 << 6,
};

class X64Emitter : public Xbyak::CodeGenerator {
 public:
  X64Emitter(X64Backend* backend, XbyakAllocator* allocator);
  virtual ~X64Emitter();

  Processor* processor() const { return processor_; }
  X64Backend* backend() const { return backend_; }

  static uintptr_t PlaceConstData();
  static void FreeConstData(uintptr_t data);

  bool Emit(GuestFunction* function, hir::HIRBuilder* builder,
            uint32_t debug_info_flags, FunctionDebugInfo* debug_info,
            void** out_code_address, size_t* out_code_size,
            std::vector<SourceMapEntry>* out_source_map);

 public:
  // Reserved:  rsp, rsi, rdi
  // Scratch:   rax/rcx/rdx
  //            xmm0-2
  // Available: rbx, r10-r15
  //            xmm4-xmm15 (save to get xmm3)
  static const int GPR_COUNT = 7;
  static const int XMM_COUNT = 12;

  static void SetupReg(const hir::Value* v, Xbyak::Reg8& r) {
    auto idx = gpr_reg_map_[v->reg.index];
    r = Xbyak::Reg8(idx);
  }
  static void SetupReg(const hir::Value* v, Xbyak::Reg16& r) {
    auto idx = gpr_reg_map_[v->reg.index];
    r = Xbyak::Reg16(idx);
  }
  static void SetupReg(const hir::Value* v, Xbyak::Reg32& r) {
    auto idx = gpr_reg_map_[v->reg.index];
    r = Xbyak::Reg32(idx);
  }
  static void SetupReg(const hir::Value* v, Xbyak::Reg64& r) {
    auto idx = gpr_reg_map_[v->reg.index];
    r = Xbyak::Reg64(idx);
  }
  static void SetupReg(const hir::Value* v, Xbyak::Xmm& r) {
    auto idx = xmm_reg_map_[v->reg.index];
    r = Xbyak::Xmm(idx);
  }

  Xbyak::Label& epilog_label() { return *epilog_label_; }

  void MarkSourceOffset(const hir::Instr* i);

  void DebugBreak();
  void Trap(uint16_t trap_type = 0);
  void UnimplementedInstr(const hir::Instr* i);

  void Call(const hir::Instr* instr, GuestFunction* function);
  void CallIndirect(const hir::Instr* instr, const Xbyak::Reg64& reg);
  void CallExtern(const hir::Instr* instr, const Function* function);
  void CallNative(void* fn);
  void CallNative(uint64_t (*fn)(void* raw_context));
  void CallNative(uint64_t (*fn)(void* raw_context, uint64_t arg0));
  void CallNative(uint64_t (*fn)(void* raw_context, uint64_t arg0),
                  uint64_t arg0);
  void CallNativeSafe(void* fn);
  void SetReturnAddress(uint64_t value);

  Xbyak::Reg64 GetNativeParam(uint32_t param);

  Xbyak::Reg64 GetContextReg();
  Xbyak::Reg64 GetMembaseReg();
  void ReloadContext();
  void ReloadMembase();

  void nop(size_t length = 1);

  // TODO(benvanik): Label for epilog (don't use strings).

  // Moves a 64bit immediate into memory.
  bool ConstantFitsIn32Reg(uint64_t v);
  void MovMem64(const Xbyak::RegExp& addr, uint64_t v);

  Xbyak::Address GetXmmConstPtr(XmmConst id);
  void LoadConstantXmm(Xbyak::Xmm dest, float v);
  void LoadConstantXmm(Xbyak::Xmm dest, double v);
  void LoadConstantXmm(Xbyak::Xmm dest, const vec128_t& v);
  Xbyak::Address StashXmm(int index, const Xbyak::Xmm& r);

  bool IsFeatureEnabled(uint32_t feature_flag) const {
    return (feature_flags_ & feature_flag) != 0;
  }

  FunctionDebugInfo* debug_info() const { return debug_info_; }

  size_t stack_size() const { return stack_size_; }

 protected:
  void* Emplace(size_t stack_size, GuestFunction* function = nullptr);
  bool Emit(hir::HIRBuilder* builder, size_t* out_stack_size);
  void EmitGetCurrentThreadId();
  void EmitTraceUserCallReturn();

 protected:
  Processor* processor_ = nullptr;
  X64Backend* backend_ = nullptr;
  X64CodeCache* code_cache_ = nullptr;
  XbyakAllocator* allocator_ = nullptr;
  Xbyak::util::Cpu cpu_;
  uint32_t feature_flags_ = 0;

  Xbyak::Label* epilog_label_ = nullptr;

  hir::Instr* current_instr_ = nullptr;

  FunctionDebugInfo* debug_info_ = nullptr;
  uint32_t debug_info_flags_ = 0;
  FunctionTraceData* trace_data_ = nullptr;
  Arena source_map_arena_;

  size_t stack_size_ = 0;

  static const uint32_t gpr_reg_map_[GPR_COUNT];
  static const uint32_t xmm_reg_map_[XMM_COUNT];
};

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_X64_X64_EMITTER_H_
