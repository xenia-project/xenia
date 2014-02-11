/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_BACKEND_X64_X64_EMITTER_H_
#define ALLOY_BACKEND_X64_X64_EMITTER_H_

#include <alloy/core.h>

#include <alloy/hir/value.h>

#include <third_party/xbyak/xbyak/xbyak.h>

XEDECLARECLASS2(alloy, hir, HIRBuilder);
XEDECLARECLASS2(alloy, hir, Instr);
XEDECLARECLASS2(alloy, runtime, DebugInfo);
XEDECLARECLASS2(alloy, runtime, Runtime);

namespace alloy {
namespace backend {
namespace x64 {

class X64Backend;
class X64CodeCache;

enum RegisterFlags {
  REG_DEST  = (1 << 0),
  REG_ABCD  = (1 << 1),
};

// Unfortunately due to the design of xbyak we have to pass this to the ctor.
class XbyakAllocator : public Xbyak::Allocator {
public:
	virtual bool useProtect() const { return false; }
};

class X64Emitter : public Xbyak::CodeGenerator {
public:
  X64Emitter(X64Backend* backend, XbyakAllocator* allocator);
  virtual ~X64Emitter();

  runtime::Runtime* runtime() const { return runtime_; }
  X64Backend* backend() const { return backend_; }

  int Initialize();

  int Emit(hir::HIRBuilder* builder,
           uint32_t debug_info_flags, runtime::DebugInfo* debug_info,
           void*& out_code_address, size_t& out_code_size);

public:
  template<typename V0>
  void BeginOp(hir::Value* v0, V0& r0, uint32_t r0_flags) {
    SetupReg(v0, r0);
  }
  template<typename V0, typename V1>
  void BeginOp(hir::Value* v0, V0& r0, uint32_t r0_flags,
               hir::Value* v1, V1& r1, uint32_t r1_flags) {
    SetupReg(v0, r0);
    SetupReg(v1, r1);
  }
  template<typename V0, typename V1, typename V2>
  void BeginOp(hir::Value* v0, V0& r0, uint32_t r0_flags,
               hir::Value* v1, V1& r1, uint32_t r1_flags,
               hir::Value* v2, V2& r2, uint32_t r2_flags) {
    SetupReg(v0, r0);
    SetupReg(v1, r1);
    SetupReg(v2, r2);
  }
  template<typename V0, typename V1, typename V2, typename V3>
  void BeginOp(hir::Value* v0, V0& r0, uint32_t r0_flags,
               hir::Value* v1, V1& r1, uint32_t r1_flags,
               hir::Value* v2, V2& r2, uint32_t r2_flags,
               hir::Value* v3, V3& r3, uint32_t r3_flags) {
    SetupReg(v0, r0);
    SetupReg(v1, r1);
    SetupReg(v2, r2);
    SetupReg(v3, r3);
  }
  template<typename V0>
  void EndOp(V0& r0) {
  }
  template<typename V0, typename V1>
  void EndOp(V0& r0, V1& r1) {
  }
  template<typename V0, typename V1, typename V2>
  void EndOp(V0& r0, V1& r1, V2& r2) {
  }
  template<typename V0, typename V1, typename V2, typename V3>
  void EndOp(V0& r0, V1& r1, V2& r2, V3& r3) {
  }

  // Reserved:  rsp
  // Scratch:   rax/rcx/rdx
  //            xmm0-1
  // Available: rbx, r12-r15 (save to get r8-r11, rbp, rsi, rdi?)
  //            xmm6-xmm15 (save to get xmm2-xmm5)
  static const int GPR_COUNT = 5;
  static const int XMM_COUNT = 10;

  static void SetupReg(hir::Value* v, Xbyak::Reg8& r) {
    auto idx = gpr_reg_map_[v->reg.index];
    r = Xbyak::Reg8(idx);
  }
  static void SetupReg(hir::Value* v, Xbyak::Reg16& r) {
    auto idx = gpr_reg_map_[v->reg.index];
    r = Xbyak::Reg16(idx);
  }
  static void SetupReg(hir::Value* v, Xbyak::Reg32& r) {
    auto idx = gpr_reg_map_[v->reg.index];
    r = Xbyak::Reg32(idx);
  }
  static void SetupReg(hir::Value* v, Xbyak::Reg64& r) {
    auto idx = gpr_reg_map_[v->reg.index];
    r = Xbyak::Reg64(idx);
  }
  static void SetupReg(hir::Value* v, Xbyak::Xmm& r) {
    auto idx = xmm_reg_map_[v->reg.index];
    r = Xbyak::Xmm(idx);
  }

  hir::Instr* Advance(hir::Instr* i);

  void MarkSourceOffset(hir::Instr* i);

  size_t stack_size() const { return stack_size_; }

protected:
  void* Emplace(size_t stack_size);
  int Emit(hir::HIRBuilder* builder, size_t& out_stack_size);

protected:
  runtime::Runtime* runtime_;
  X64Backend*       backend_;
  X64CodeCache*     code_cache_;
  XbyakAllocator*   allocator_;

  hir::Instr* current_instr_;

  size_t    source_map_count_;
  Arena     source_map_arena_;

  size_t    stack_size_;

  static const uint32_t gpr_reg_map_[GPR_COUNT];
  static const uint32_t xmm_reg_map_[XMM_COUNT];
};


}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_X64_EMITTER_H_
