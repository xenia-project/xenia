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
    uint32_t v0_idx;
    FindFreeRegs(v0, v0_idx, r0_flags);
    SetupReg(v0_idx, r0);
  }
  template<typename V0, typename V1>
  void BeginOp(hir::Value* v0, V0& r0, uint32_t r0_flags,
               hir::Value* v1, V1& r1, uint32_t r1_flags) {
    uint32_t v0_idx, v1_idx;
    FindFreeRegs(v0, v0_idx, r0_flags,
                 v1, v1_idx, r1_flags);
    SetupReg(v0_idx, r0);
    SetupReg(v1_idx, r1);
  }
  template<typename V0, typename V1, typename V2>
  void BeginOp(hir::Value* v0, V0& r0, uint32_t r0_flags,
               hir::Value* v1, V1& r1, uint32_t r1_flags,
               hir::Value* v2, V2& r2, uint32_t r2_flags) {
    uint32_t v0_idx, v1_idx, v2_idx;
    FindFreeRegs(v0, v0_idx, r0_flags,
                 v1, v1_idx, r1_flags,
                 v2, v2_idx, r2_flags);
    SetupReg(v0_idx, r0);
    SetupReg(v1_idx, r1);
    SetupReg(v2_idx, r2);
  }
  template<typename V0, typename V1, typename V2, typename V3>
  void BeginOp(hir::Value* v0, V0& r0, uint32_t r0_flags,
               hir::Value* v1, V1& r1, uint32_t r1_flags,
               hir::Value* v2, V2& r2, uint32_t r2_flags,
               hir::Value* v3, V3& r3, uint32_t r3_flags) {
    uint32_t v0_idx, v1_idx, v2_idx, v3_idx;
    FindFreeRegs(v0, v0_idx, r0_flags,
                 v1, v1_idx, r1_flags,
                 v2, v2_idx, r2_flags,
                 v3, v3_idx, r3_flags);
    SetupReg(v0_idx, r0);
    SetupReg(v1_idx, r1);
    SetupReg(v2_idx, r2);
    SetupReg(v3_idx, r3);
  }
  template<typename V0>
  void EndOp(V0& r0) {
    reg_state_.active_regs = reg_state_.active_regs ^ GetRegBit(r0);
  }
  template<typename V0, typename V1>
  void EndOp(V0& r0, V1& r1) {
    reg_state_.active_regs = reg_state_.active_regs ^ (
        GetRegBit(r0) | GetRegBit(r1));
  }
  template<typename V0, typename V1, typename V2>
  void EndOp(V0& r0, V1& r1, V2& r2) {
    reg_state_.active_regs = reg_state_.active_regs ^ (
        GetRegBit(r0) | GetRegBit(r1) | GetRegBit(r2));
  }
  template<typename V0, typename V1, typename V2, typename V3>
  void EndOp(V0& r0, V1& r1, V2& r2, V3& r3) {
    reg_state_.active_regs = reg_state_.active_regs ^ (
        GetRegBit(r0) | GetRegBit(r1) | GetRegBit(r2) | GetRegBit(r3));
  }

  void ResetRegisters(uint32_t reserved_regs);
  void EvictStaleRegisters();

  void FindFreeRegs(hir::Value* v0, uint32_t& v0_idx, uint32_t v0_flags);
  void FindFreeRegs(hir::Value* v0, uint32_t& v0_idx, uint32_t v0_flags,
                    hir::Value* v1, uint32_t& v1_idx, uint32_t v1_flags);
  void FindFreeRegs(hir::Value* v0, uint32_t& v0_idx, uint32_t v0_flags,
                    hir::Value* v1, uint32_t& v1_idx, uint32_t v1_flags,
                    hir::Value* v2, uint32_t& v2_idx, uint32_t v2_flags);
  void FindFreeRegs(hir::Value* v0, uint32_t& v0_idx, uint32_t v0_flags,
                    hir::Value* v1, uint32_t& v1_idx, uint32_t v1_flags,
                    hir::Value* v2, uint32_t& v2_idx, uint32_t v2_flags,
                    hir::Value* v3, uint32_t& v3_idx, uint32_t v3_flags);

  static void SetupReg(uint32_t idx, Xbyak::Reg8& r) { r = Xbyak::Reg8(idx); }
  static void SetupReg(uint32_t idx, Xbyak::Reg16& r) { r = Xbyak::Reg16(idx); }
  static void SetupReg(uint32_t idx, Xbyak::Reg32& r) { r = Xbyak::Reg32(idx); }
  static void SetupReg(uint32_t idx, Xbyak::Reg64& r) { r = Xbyak::Reg64(idx); }
  static void SetupReg(uint32_t idx, Xbyak::Xmm& r) { r = Xbyak::Xmm(idx - 16); }
  static uint32_t GetRegBit(const Xbyak::Reg8& r) { return 1 << r.getIdx(); }
  static uint32_t GetRegBit(const Xbyak::Reg16& r) { return 1 << r.getIdx(); }
  static uint32_t GetRegBit(const Xbyak::Reg32& r) { return 1 << r.getIdx(); }
  static uint32_t GetRegBit(const Xbyak::Reg64& r) { return 1 << r.getIdx(); }
  static uint32_t GetRegBit(const Xbyak::Xmm& r) { return 1 << (16 + r.getIdx()); }

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

  struct {
    // Registers currently active within a begin/end op block. These
    // cannot be reused.
    uint32_t    active_regs;
    // Registers with values in them.
    uint32_t    live_regs;
    // Current register values.
    hir::Value* reg_values[32];
  } reg_state_;
  hir::Instr* current_instr_;

  size_t    source_map_count_;
  Arena     source_map_arena_;

  size_t    stack_size_;
};


}  // namespace x64
}  // namespace backend
}  // namespace alloy


#endif  // ALLOY_BACKEND_X64_X64_EMITTER_H_
