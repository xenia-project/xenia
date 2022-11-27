/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
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
#include "xenia/cpu/xex_module.h"
#include "xenia/memory.h"
// NOTE: must be included last as it expects windows.h to already be included.
#include "third_party/xbyak/xbyak/xbyak.h"
#include "third_party/xbyak/xbyak/xbyak_util.h"
#include "x64_amdfx_extensions.h"
namespace xe {
namespace cpu {
class Processor;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {
using namespace amd64;
class X64Backend;
class X64CodeCache;

struct EmitFunctionInfo;

enum RegisterFlags {
  REG_DEST = (1 << 0),
  REG_ABCD = (1 << 1),
};
/*
    SSE/AVX/AVX512 has seperate move instructions/shuffle instructions for float
   data and int data for a reason most processors implement two distinct
   pipelines, one for the integer domain and one for the floating point domain
    currently, xenia makes no distinction between the two. Crossing domains is
   expensive. On Zen processors the penalty is one cycle each time you cross,
   plus the two pipelines need to synchronize Often xenia will emit an integer
   instruction, then a floating instruction, then integer again. this
   effectively adds at least two cycles to the time taken These values will in
   the future be used as tags to operations that tell them which domain to
   operate in, if its at all possible to avoid crossing
*/
enum class SimdDomain : uint32_t {
  FLOATING,
  INTEGER,
  DONTCARE,
  CONFLICTING  // just used as a special result for PickDomain, different from
               // dontcare (dontcare means we just dont know the domain,
               // CONFLICTING means its used in multiple domains)
};

enum class MXCSRMode : uint32_t { Unknown, Fpu, Vmx };
XE_MAYBE_UNUSED
static SimdDomain PickDomain2(SimdDomain dom1, SimdDomain dom2) {
  if (dom1 == dom2) {
    return dom1;
  }
  if (dom1 == SimdDomain::DONTCARE) {
    return dom2;
  }
  if (dom2 == SimdDomain::DONTCARE) {
    return dom1;
  }
  return SimdDomain::CONFLICTING;
}
enum XmmConst {
  XMMZero = 0,
  XMMByteSwapMask,
  XMMOne,
  XMMOnePD,
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
  XMMPackULONG_4202020_MinUnpacked,
  XMMPackULONG_4202020_MaxUnpacked,
  XMMPackULONG_4202020_MaskUnpacked,
  XMMPackULONG_4202020_PermuteXZ,
  XMMPackULONG_4202020_PermuteYW,
  XMMUnpackULONG_4202020_Permute,
  XMMUnpackULONG_4202020_Overflow,
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
  XMMQNaN,
  XMMInt127,
  XMM2To32,
  XMMFloatInf,
  XMMIntsToBytes,
  XMMShortsToBytes,
  XMMLVSLTableBase,
  XMMLVSRTableBase,
  XMMSingleDenormalMask,
  XMMThreeFloatMask,  // for clearing the fourth float prior to DOT_PRODUCT_3
  XMMF16UnpackLCPI2,  // 0x38000000, 1/ 32768
  XMMF16UnpackLCPI3,  // 0x0x7fe000007fe000
  XMMF16PackLCPI0,
  XMMF16PackLCPI2,
  XMMF16PackLCPI3,
  XMMF16PackLCPI4,
  XMMF16PackLCPI5,
  XMMF16PackLCPI6,
  XMMXOPByteShiftMask,
  XMMXOPWordShiftMask,
  XMMXOPDwordShiftMask,
  XMMLVLShuffle,
  XMMLVRCmp16,
  XMMSTVLShuffle,
  XMMSTVRSwapMask,  // swapwordmask with bit 7 set
  XMMVSRShlByteshuf,
  XMMVSRMask

};
using amdfx::xopcompare_e;
using Xbyak::Xmm;
// X64Backend specific Instr->runtime_flags
enum : uint32_t {
  INSTR_X64_FLAGS_ELIMINATED =
      1,  // another sequence marked this instruction as not needing codegen,
          // meaning they likely already handled it
};

// Unfortunately due to the design of xbyak we have to pass this to the ctor.
class XbyakAllocator : public Xbyak::Allocator {
 public:
  virtual bool useProtect() const { return false; }
};

class X64Emitter;
using TailEmitCallback = std::function<void(X64Emitter& e, Xbyak::Label& lbl)>;
struct TailEmitter {
  Xbyak::Label label;
  uint32_t alignment;
  TailEmitCallback func;
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
  static constexpr size_t kStashOffset = 32;
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

  Xbyak::Reg64 GetContextReg() const;
  Xbyak::Reg64 GetMembaseReg() const;
  bool CanUseMembaseLow32As0() const { return may_use_membase32_as_zero_reg_; }
  void ReloadMembase();

  void nop(size_t length = 1);

  // Moves a 64bit immediate into memory.
  bool ConstantFitsIn32Reg(uint64_t v);
  void MovMem64(const Xbyak::RegExp& addr, uint64_t v);

  Xbyak::Address GetXmmConstPtr(XmmConst id);
  Xbyak::Address GetBackendCtxPtr(int offset_in_x64backendctx) const;

  void LoadConstantXmm(Xbyak::Xmm dest, float v);
  void LoadConstantXmm(Xbyak::Xmm dest, double v);
  void LoadConstantXmm(Xbyak::Xmm dest, const vec128_t& v);
  Xbyak::Address StashXmm(int index, const Xbyak::Xmm& r);
  Xbyak::Address StashConstantXmm(int index, float v);
  Xbyak::Address StashConstantXmm(int index, double v);
  Xbyak::Address StashConstantXmm(int index, const vec128_t& v);
  Xbyak::Address GetBackendFlagsPtr() const;
  void* FindByteConstantOffset(unsigned bytevalue);
  void* FindWordConstantOffset(unsigned wordvalue);
  void* FindDwordConstantOffset(unsigned bytevalue);
  void* FindQwordConstantOffset(uint64_t bytevalue);
  bool IsFeatureEnabled(uint64_t feature_flag) const {
    return (feature_flags_ & feature_flag) == feature_flag;
  }

  Xbyak::Label& AddToTail(TailEmitCallback callback, uint32_t alignment = 0);
  Xbyak::Label& NewCachedLabel();

  void PushStackpoint();
  void PopStackpoint();

  void EnsureSynchronizedGuestAndHostStack();
  FunctionDebugInfo* debug_info() const { return debug_info_; }

  size_t stack_size() const { return stack_size_; }
  SimdDomain DeduceSimdDomain(const hir::Value* for_value);

  void ForgetMxcsrMode() { mxcsr_mode_ = MXCSRMode::Unknown; }
  /*
        returns true if had to load mxcsr. DOT_PRODUCT can use this to skip
     clearing the overflow flag, as it will never be set in the vmx fpscr
  */
  bool ChangeMxcsrMode(
      MXCSRMode new_mode,
      bool already_set = false);  // already_set means that the caller already
                                  // did vldmxcsr, used for SET_ROUNDING_MODE

  void LoadFpuMxcsrDirect();  // unsafe, does not change mxcsr_mode_
  void LoadVmxMxcsrDirect();  // unsafe, does not change mxcsr_mode_

  XexModule* GuestModule() { return guest_module_; }

  void EmitProfilerEpilogue();

  void EmitXOP(amdfx::xop_t xoperation) {
    xoperation.ForeachByte([this](uint8_t b) { this->db(b); });
  }

  void vpcmov(Xmm dest, Xmm src1, Xmm src2, Xmm selector) {
    auto xop_bytes = amdfx::operations::vpcmov(
        dest.getIdx(), src1.getIdx(), src2.getIdx(), selector.getIdx());
    EmitXOP(xop_bytes);
  }

  void vpperm(Xmm dest, Xmm src1, Xmm src2, Xmm selector) {
    auto xop_bytes = amdfx::operations::vpperm(
        dest.getIdx(), src1.getIdx(), src2.getIdx(), selector.getIdx());
    EmitXOP(xop_bytes);
  }

#define DEFINECOMPARE(name)                                                \
  void name(Xmm dest, Xmm src1, Xmm src2, xopcompare_e compareop) {        \
    auto xop_bytes = amdfx::operations::name(dest.getIdx(), src1.getIdx(), \
                                             src2.getIdx(), compareop);    \
    EmitXOP(xop_bytes);                                                    \
  }
  DEFINECOMPARE(vpcomb);
  DEFINECOMPARE(vpcomub);
  DEFINECOMPARE(vpcomw);
  DEFINECOMPARE(vpcomuw);
  DEFINECOMPARE(vpcomd);
  DEFINECOMPARE(vpcomud);
  DEFINECOMPARE(vpcomq);
  DEFINECOMPARE(vpcomuq);
#undef DEFINECOMPARE

#define DEFINESHIFTER(name)                                                   \
  void name(Xmm dest, Xmm src1, Xmm src2) {                                   \
    auto xop_bytes =                                                          \
        amdfx::operations::name(dest.getIdx(), src1.getIdx(), src2.getIdx()); \
    EmitXOP(xop_bytes);                                                       \
  }

  DEFINESHIFTER(vprotb)
  DEFINESHIFTER(vprotw)
  DEFINESHIFTER(vprotd)
  DEFINESHIFTER(vprotq)

  DEFINESHIFTER(vpshab)
  DEFINESHIFTER(vpshaw)
  DEFINESHIFTER(vpshad)
  DEFINESHIFTER(vpshaq)

  DEFINESHIFTER(vpshlb)
  DEFINESHIFTER(vpshlw)
  DEFINESHIFTER(vpshld)
  DEFINESHIFTER(vpshlq)

 protected:
  void* Emplace(const EmitFunctionInfo& func_info,
                GuestFunction* function = nullptr);
  bool Emit(hir::HIRBuilder* builder, EmitFunctionInfo& func_info);
  void EmitGetCurrentThreadId();
  void EmitTraceUserCallReturn();
  static void HandleStackpointOverflowError(ppc::PPCContext* context);
 protected:
  Processor* processor_ = nullptr;
  X64Backend* backend_ = nullptr;
  X64CodeCache* code_cache_ = nullptr;
  XbyakAllocator* allocator_ = nullptr;
  XexModule* guest_module_ = nullptr;
  bool synchronize_stack_on_next_instruction_ = false;
  Xbyak::util::Cpu cpu_;
  uint64_t feature_flags_ = 0;
  uint32_t current_guest_function_ = 0;
  Xbyak::Label* epilog_label_ = nullptr;

  hir::Instr* current_instr_ = nullptr;

  FunctionDebugInfo* debug_info_ = nullptr;
  uint32_t debug_info_flags_ = 0;
  FunctionTraceData* trace_data_ = nullptr;
  Arena source_map_arena_;

  size_t stack_size_ = 0;

  static const uint32_t gpr_reg_map_[GPR_COUNT];
  static const uint32_t xmm_reg_map_[XMM_COUNT];
  /*
    set to true if the low 32 bits of membase == 0.
    only really advantageous if you are storing 32 bit 0 to a displaced address,
    which would have to represent 0 as 4 bytes
  */
  bool may_use_membase32_as_zero_reg_;
  std::vector<TailEmitter> tail_code_;
  std::vector<Xbyak::Label*>
      label_cache_;  // for creating labels that need to be referenced much
                     // later by tail emitters
  MXCSRMode mxcsr_mode_ = MXCSRMode::Unknown;
};

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_X64_X64_EMITTER_H_
