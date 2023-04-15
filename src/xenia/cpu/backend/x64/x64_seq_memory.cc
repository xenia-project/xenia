/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Xenia Developers. All rights reserved.                      *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/x64/x64_sequences.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/cvar.h"
#include "xenia/base/memory.h"
#include "xenia/cpu/backend/x64/x64_backend.h"
#include "xenia/cpu/backend/x64/x64_op.h"
#include "xenia/cpu/backend/x64/x64_stack_layout.h"
#include "xenia/cpu/backend/x64/x64_tracers.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"
DEFINE_bool(
    elide_e0_check, false,
    "Eliminate e0 check on some memory accesses, like to r13(tls) or r1(sp)",
    "CPU");
DEFINE_bool(enable_rmw_context_merging, false,
            "Permit merging read-modify-write HIR instr sequences together "
            "into x86 instructions that use a memory operand.",
            "x64");
DEFINE_bool(emit_mmio_aware_stores_for_recorded_exception_addresses, true,
            "Uses info gathered via record_mmio_access_exceptions to emit "
            "special stores that are faster than trapping the exception",
            "CPU");

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

struct LoadModStore {
  const hir::Instr* load;
  hir::Instr* modify;
  hir::Instr* store;

  bool is_constant[3];
  void Consume();
};
void LoadModStore::Consume() {
  modify->backend_flags |= INSTR_X64_FLAGS_ELIMINATED;
  store->backend_flags |= INSTR_X64_FLAGS_ELIMINATED;
}
static bool GetLoadModStore(const hir::Instr* loadinsn, LoadModStore* out) {
  if (IsTracingData()) {
    return false;
  }
  // if (!loadinsn->dest->HasSingleUse()) {
  // allow the value to be used multiple times, as long as it is by the same
  // instruction
  if (!loadinsn->dest->AllUsesByOneInsn()) {
    return false;
  }
  hir::Instr* use = loadinsn->dest->use_head->instr;

  if (!use->dest || !use->dest->HasSingleUse() ||
      use->GetNonFakePrev() != loadinsn) {
    return false;
  }

  hir::Instr* shouldbstore = use->dest->use_head->instr;

  if (shouldbstore->dest || shouldbstore->GetNonFakePrev() != use) {
    return false;  // store insns have no destination
  }
  use->VisitValueOperands([out](Value* v, uint32_t idx) {
    out->is_constant[idx] = v->IsConstant();
  });
  out->load = loadinsn;
  out->modify = use;
  out->store = shouldbstore;
  return true;
}
struct LoadModStoreContext : public LoadModStore {
  uint64_t offset;  // ctx offset
  TypeName type;
  Opcode op;
  bool is_commutative;
  bool is_unary;
  bool is_binary;
  bool
      binary_uses_twice;  // true if binary_other == our value. (for instance,
                          // add r11, r10, r10, which can be gen'ed for r10 * 2)
  hir::Value* binary_other;

  hir::Value::ConstantValue* other_const;
  uint32_t other_index;
};
static bool GetLoadModStoreContext(const hir::Instr* loadinsn,
                                   LoadModStoreContext* out) {
  if (!cvars::enable_rmw_context_merging) {
    return false;
  }
  if (!GetLoadModStore(loadinsn, out)) {
    return false;
  }

  if (out->load->opcode->num != OPCODE_LOAD_CONTEXT ||
      out->store->opcode->num != OPCODE_STORE_CONTEXT) {
    return false;
  }

  if (out->modify->opcode->flags &
      (OPCODE_FLAG_VOLATILE | OPCODE_FLAG_MEMORY)) {
    return false;
  }
  uint64_t offs = out->load->src1.offset;

  if (offs != out->store->src1.offset) {
    return false;
  }

  TypeName typ = out->load->dest->type;
  // can happen if op is a conversion
  if (typ != out->store->src2.value->type) {
    return false;
  }
  /*
        set up a whole bunch of convenience fields for the caller
  */
  out->offset = offs;
  out->type = typ;
  const OpcodeInfo& opinf = *out->modify->opcode;
  out->op = opinf.num;
  out->is_commutative = opinf.flags & OPCODE_FLAG_COMMUNATIVE;
  out->is_unary = IsOpcodeUnaryValue(opinf.signature);
  out->is_binary = IsOpcodeBinaryValue(opinf.signature);
  out->binary_uses_twice = false;
  out->binary_other = nullptr;
  out->other_const = nullptr;
  out->other_index = ~0U;
  if (out->is_binary) {
    if (out->modify->src1.value == out->load->dest) {
      out->binary_other = out->modify->src2.value;
      out->other_index = 1;
    } else {
      out->binary_other = out->modify->src1.value;
      out->other_index = 0;
    }
    if (out->binary_other && out->is_constant[out->other_index]) {
      out->other_const = &out->binary_other->constant;
    }
    if (out->binary_other == out->load->dest) {
      out->binary_uses_twice = true;
    }
  }
  return true;
}
volatile int anchor_memory = 0;

static void Do0x1000Add(X64Emitter& e, Reg32 reg) {
  e.add(reg, e.GetBackendCtxPtr(offsetof(X64BackendContext, Ox1000)));
  // e.add(reg, 0x1000);
}

// Note: all types are always aligned in the context.
RegExp ComputeContextAddress(X64Emitter& e, const OffsetOp& offset) {
  return e.GetContextReg() + offset.value;
}
static bool is_eo_def(const hir::Value* v) {
  if (v->def) {
    auto df = v->def;
    if (df->opcode == &OPCODE_LOAD_CONTEXT_info) {
      size_t offs = df->src1.offset;
      if (offs == offsetof(ppc::PPCContext_s, r[1]) ||
          offs == offsetof(ppc::PPCContext_s, r[13])) {
        return true;
      }
    } else if (df->opcode == &OPCODE_ASSIGN_info) {
      return is_eo_def(df->src1.value);
    }
  }
  return false;
}

template <typename T>
static bool is_definitely_not_eo(const T& v) {
  if (!cvars::elide_e0_check) {
    return false;
  }

  return is_eo_def(v.value);
}
// Note: most *should* be aligned, but needs to be checked!
template <typename T>
RegExp ComputeMemoryAddress(X64Emitter& e, const T& guest) {
  if (guest.is_constant) {
    // TODO(benvanik): figure out how to do this without a temp.
    // Since the constant is often 0x8... if we tried to use that as a
    // displacement it would be sign extended and mess things up.
    uint32_t address = static_cast<uint32_t>(guest.constant());
    if (address < 0x80000000) {
      return e.GetMembaseReg() + address;
    } else {
      if (address >= 0xE0000000 &&
          xe::memory::allocation_granularity() > 0x1000) {
        e.mov(e.eax, address + 0x1000);
      } else {
        e.mov(e.eax, address);
      }
      return e.GetMembaseReg() + e.rax;
    }
  } else {
    if (xe::memory::allocation_granularity() > 0x1000 &&
        !is_definitely_not_eo(guest)) {
      // Emulate the 4 KB physical address offset in 0xE0000000+ when can't do
      // it via memory mapping.
      Xbyak::Label& jmpback = e.NewCachedLabel();

      e.mov(e.eax, guest.reg().cvt32());

      e.cmp(guest.reg().cvt32(), e.GetContextReg().cvt32());

      Xbyak::Label& fixup_label =
          e.AddToTail([&jmpback](X64Emitter& e, Xbyak::Label& our_tail_label) {
            e.L(our_tail_label);
            Do0x1000Add(e, e.eax);
            e.jmp(jmpback, e.T_NEAR);
          });
      e.jae(fixup_label, e.T_NEAR);

      e.L(jmpback);
      return e.GetMembaseReg() + e.rax;

    } else {
      // Clear the top 32 bits, as they are likely garbage.
      // TODO(benvanik): find a way to avoid doing this.
      e.mov(e.eax, guest.reg().cvt32());
    }
    return e.GetMembaseReg() + e.rax;
  }
}
template <typename T>
RegExp ComputeMemoryAddressOffset(X64Emitter& e, const T& guest,
                                  const T& offset) {
  assert_true(offset.is_constant);
  int32_t offset_const = static_cast<int32_t>(offset.constant());
  if (offset_const == 0) {
    return ComputeMemoryAddress(e, guest);
  }
  if (guest.is_constant) {
    uint32_t address = static_cast<uint32_t>(guest.constant());
    address += offset_const;
    if (address < 0x80000000) {
      return e.GetMembaseReg() + address;
    } else {
      if (address >= 0xE0000000 &&
          xe::memory::allocation_granularity() > 0x1000) {
        e.mov(e.eax, address + 0x1000);
      } else {
        e.mov(e.eax, address);
      }
      return e.GetMembaseReg() + e.rax;
    }
  } else {
    if (xe::memory::allocation_granularity() > 0x1000 &&
        !is_definitely_not_eo(guest)) {
      // Emulate the 4 KB physical address offset in 0xE0000000+ when can't do
      // it via memory mapping.

      // todo: do branching or use an alt membase and cmov

      Xbyak::Label& tmplbl = e.NewCachedLabel();

      e.lea(e.edx, e.ptr[guest.reg().cvt32() + offset_const]);

      e.cmp(e.edx, e.GetContextReg().cvt32());

      Xbyak::Label& fixup_label =
          e.AddToTail([&tmplbl](X64Emitter& e, Xbyak::Label& our_tail_label) {
            e.L(our_tail_label);

            Do0x1000Add(e, e.edx);

            e.jmp(tmplbl, e.T_NEAR);
          });
      e.jae(fixup_label, e.T_NEAR);

      e.L(tmplbl);
      return e.GetMembaseReg() + e.rdx;

    } else {
      // Clear the top 32 bits, as they are likely garbage.
      // TODO(benvanik): find a way to avoid doing this.

      e.mov(e.eax, guest.reg().cvt32());
    }
    return e.GetMembaseReg() + e.rax + offset_const;
  }
}

// ============================================================================
// OPCODE_ATOMIC_EXCHANGE
// ============================================================================
// Note that the address we use here is a real, host address!
// This is weird, and should be fixed.
template <typename SEQ, typename REG, typename ARGS>
void EmitAtomicExchangeXX(X64Emitter& e, const ARGS& i) {
  if (i.dest == i.src1) {
    e.mov(e.rax, i.src1);
    if (i.dest != i.src2) {
      if (i.src2.is_constant) {
        e.mov(i.dest, i.src2.constant());
      } else {
        e.mov(i.dest, i.src2);
      }
    }
    e.lock();
    e.xchg(e.dword[e.rax], i.dest);
  } else {
    if (i.dest != i.src2) {
      if (i.src2.is_constant) {
        e.mov(i.dest, i.src2.constant());
      } else {
        e.mov(i.dest, i.src2);
      }
    }
    e.lock();
    e.xchg(e.dword[i.src1.reg()], i.dest);
  }
}
struct ATOMIC_EXCHANGE_I8
    : Sequence<ATOMIC_EXCHANGE_I8,
               I<OPCODE_ATOMIC_EXCHANGE, I8Op, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAtomicExchangeXX<ATOMIC_EXCHANGE_I8, Reg8>(e, i);
  }
};
struct ATOMIC_EXCHANGE_I16
    : Sequence<ATOMIC_EXCHANGE_I16,
               I<OPCODE_ATOMIC_EXCHANGE, I16Op, I64Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAtomicExchangeXX<ATOMIC_EXCHANGE_I16, Reg16>(e, i);
  }
};
struct ATOMIC_EXCHANGE_I32
    : Sequence<ATOMIC_EXCHANGE_I32,
               I<OPCODE_ATOMIC_EXCHANGE, I32Op, I64Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAtomicExchangeXX<ATOMIC_EXCHANGE_I32, Reg32>(e, i);
  }
};
struct ATOMIC_EXCHANGE_I64
    : Sequence<ATOMIC_EXCHANGE_I64,
               I<OPCODE_ATOMIC_EXCHANGE, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAtomicExchangeXX<ATOMIC_EXCHANGE_I64, Reg64>(e, i);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ATOMIC_EXCHANGE, ATOMIC_EXCHANGE_I8,
                     ATOMIC_EXCHANGE_I16, ATOMIC_EXCHANGE_I32,
                     ATOMIC_EXCHANGE_I64);

struct LVL_V128 : Sequence<LVL_V128, I<OPCODE_LVL, V128Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(e.edx, 0xf);

    e.lea(e.rcx, e.ptr[ComputeMemoryAddress(e, i.src1)]);
    e.mov(e.eax, 0xf);

    e.and_(e.eax, e.ecx);
    e.or_(e.rcx, e.rdx);
    e.vmovd(e.xmm0, e.eax);

    e.xor_(e.rcx, e.rdx);
    e.vpxor(e.xmm1, e.xmm1);
    e.vmovdqa(e.xmm3, e.ptr[e.rcx]);
    e.vmovdqa(e.xmm2, e.GetXmmConstPtr(XMMLVLShuffle));
    e.vmovdqa(i.dest, e.GetXmmConstPtr(XMMPermuteControl15));
    e.vpshufb(e.xmm0, e.xmm0, e.xmm1);

    e.vpaddb(e.xmm2, e.xmm0);

    e.vpcmpgtb(e.xmm1, e.xmm2, i.dest);
    e.vpor(e.xmm0, e.xmm1, e.xmm2);
    e.vpshufb(i.dest, e.xmm3, e.xmm0);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LVL, LVL_V128);

struct LVR_V128 : Sequence<LVR_V128, I<OPCODE_LVR, V128Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Xbyak::Label endpoint{};
    // todo: bailout instead? dont know how frequently the zero skip happens
    e.vpxor(i.dest, i.dest);
    e.mov(e.edx, 0xf);

    e.lea(e.rcx, e.ptr[ComputeMemoryAddress(e, i.src1)]);
    e.mov(e.eax, 0xf);

    e.and_(e.eax, e.ecx);
    e.jz(endpoint);
    e.or_(e.rcx, e.rdx);
    e.vmovd(e.xmm0, e.eax);

    e.xor_(e.rcx, e.rdx);
    e.vpxor(e.xmm1, e.xmm1);
    e.vmovdqa(e.xmm3, e.ptr[e.rcx]);
    e.vmovdqa(e.xmm2, e.GetXmmConstPtr(XMMLVLShuffle));
    e.vmovdqa(i.dest, e.GetXmmConstPtr(XMMLVRCmp16));
    e.vpshufb(e.xmm0, e.xmm0, e.xmm1);

    e.vpaddb(e.xmm2, e.xmm0);

    e.vpcmpgtb(e.xmm1, i.dest, e.xmm2);
    e.vpor(e.xmm0, e.xmm1, e.xmm2);
    e.vpshufb(i.dest, e.xmm3, e.xmm0);
    e.L(endpoint);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LVR, LVR_V128);

struct STVL_V128 : Sequence<STVL_V128, I<OPCODE_STVL, VoidOp, I64Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(e.ecx, 15);
    e.mov(e.edx, e.ecx);
    e.lea(e.rax, e.ptr[ComputeMemoryAddress(e, i.src1)]);
    e.and_(e.ecx, e.eax);
    e.vmovd(e.xmm0, e.ecx);
    e.not_(e.rdx);
    e.and_(e.rax, e.rdx);
    e.vmovdqa(e.xmm1, e.GetXmmConstPtr(XMMSTVLShuffle));
    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      e.vpbroadcastb(e.xmm3, e.xmm0);
    } else {
      e.vpshufb(e.xmm3, e.xmm0, e.GetXmmConstPtr(XMMZero));
    }
    e.vpsubb(e.xmm0, e.xmm1, e.xmm3);
    e.vpxor(e.xmm1, e.xmm0,
            e.GetXmmConstPtr(XMMSwapWordMask));  // xmm1 from now on will be our
                                                 // selector for blend/shuffle

    Xmm src2 = GetInputRegOrConstant(e, i.src2, e.xmm0);

    e.vpshufb(e.xmm2, src2, e.xmm1);
    e.vpblendvb(e.xmm3, e.xmm2, e.ptr[e.rax], e.xmm1);
    e.vmovdqa(e.ptr[e.rax], e.xmm3);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STVL, STVL_V128);

struct STVR_V128 : Sequence<STVR_V128, I<OPCODE_STVR, VoidOp, I64Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Xbyak::Label skipper{};
    e.mov(e.ecx, 15);
    e.mov(e.edx, e.ecx);
    e.lea(e.rax, e.ptr[ComputeMemoryAddress(e, i.src1)]);
    e.and_(e.ecx, e.eax);
    e.jz(skipper);
    e.vmovd(e.xmm0, e.ecx);
    e.not_(e.rdx);
    e.and_(e.rax, e.rdx);
    e.vmovdqa(e.xmm1, e.GetXmmConstPtr(XMMSTVLShuffle));
    // todo: maybe a table lookup might be a better idea for getting the
    // shuffle/blend

    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      e.vpbroadcastb(e.xmm3, e.xmm0);
    } else {
      e.vpshufb(e.xmm3, e.xmm0, e.GetXmmConstPtr(XMMZero));
    }
    e.vpsubb(e.xmm0, e.xmm1, e.xmm3);
    e.vpxor(e.xmm1, e.xmm0,
            e.GetXmmConstPtr(XMMSTVRSwapMask));  // xmm1 from now on will be our
                                                 // selector for blend/shuffle

    Xmm src2 = GetInputRegOrConstant(e, i.src2, e.xmm0);

    e.vpshufb(e.xmm2, src2, e.xmm1);
    e.vpblendvb(e.xmm3, e.xmm2, e.ptr[e.rax], e.xmm1);
    e.vmovdqa(e.ptr[e.rax], e.xmm3);
    e.L(skipper);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STVR, STVR_V128);

struct RESERVED_LOAD_INT32
    : Sequence<RESERVED_LOAD_INT32, I<OPCODE_RESERVED_LOAD, I32Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // should use phys addrs, not virtual addrs!

    // try_acquire_reservation_helper_ doesnt spoil rax
    e.lea(e.rax, e.ptr[ComputeMemoryAddress(e, i.src1)]);
    // begin acquiring exclusive access to the location
    // we will do a load first, but we'll need exclusive access once we do our
    // atomic op in the store
    e.prefetchw(e.ptr[e.rax]);
    e.mov(e.ecx, i.src1.reg().cvt32());
    e.call(e.backend()->try_acquire_reservation_helper_);
    e.mov(i.dest, e.dword[e.rax]);

    e.mov(
        e.GetBackendCtxPtr(offsetof(X64BackendContext, cached_reserve_value_)),
        i.dest.reg().cvt64());
  }
};

struct RESERVED_LOAD_INT64
    : Sequence<RESERVED_LOAD_INT64, I<OPCODE_RESERVED_LOAD, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // try_acquire_reservation_helper_ doesnt spoil rax
    e.lea(e.rax, e.ptr[ComputeMemoryAddress(e, i.src1)]);
    e.mov(e.ecx, i.src1.reg().cvt32());
    // begin acquiring exclusive access to the location
    // we will do a load first, but we'll need exclusive access once we do our
    // atomic op in the store
    e.prefetchw(e.ptr[e.rax]);

    e.call(e.backend()->try_acquire_reservation_helper_);
    e.mov(i.dest, e.qword[ComputeMemoryAddress(e, i.src1)]);

    e.mov(
        e.GetBackendCtxPtr(offsetof(X64BackendContext, cached_reserve_value_)),
        i.dest.reg());
  }
};

EMITTER_OPCODE_TABLE(OPCODE_RESERVED_LOAD, RESERVED_LOAD_INT32,
                     RESERVED_LOAD_INT64);

// address, value

struct RESERVED_STORE_INT32
    : Sequence<RESERVED_STORE_INT32,
               I<OPCODE_RESERVED_STORE, I8Op, I64Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // edx=guest addr
    // r9 = host addr
    // r8 = value
    // if ZF is set and CF is set, we succeeded
    e.mov(e.ecx, i.src1.reg().cvt32());
    e.lea(e.r9, e.ptr[ComputeMemoryAddress(e, i.src1)]);
    e.mov(e.r8d, i.src2);
    e.call(e.backend()->reserved_store_32_helper);
    e.setz(i.dest);
  }
};

struct RESERVED_STORE_INT64
    : Sequence<RESERVED_STORE_INT64,
               I<OPCODE_RESERVED_STORE, I8Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(e.ecx, i.src1.reg().cvt32());
    e.lea(e.r9, e.ptr[ComputeMemoryAddress(e, i.src1)]);
    e.mov(e.r8, i.src2);
    e.call(e.backend()->reserved_store_64_helper);
    e.setz(i.dest);
  }
};

EMITTER_OPCODE_TABLE(OPCODE_RESERVED_STORE, RESERVED_STORE_INT32,
                     RESERVED_STORE_INT64);

// ============================================================================
// OPCODE_ATOMIC_COMPARE_EXCHANGE
// ============================================================================
struct ATOMIC_COMPARE_EXCHANGE_I32
    : Sequence<ATOMIC_COMPARE_EXCHANGE_I32,
               I<OPCODE_ATOMIC_COMPARE_EXCHANGE, I8Op, I64Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(e.eax, i.src2);
    if (xe::memory::allocation_granularity() > 0x1000) {
      // Emulate the 4 KB physical address offset in 0xE0000000+ when can't do
      // it via memory mapping.
      e.mov(e.ecx, i.src1.reg().cvt32());
      e.cmp(i.src1.reg().cvt32(), e.GetContextReg().cvt32());
      Xbyak::Label& backtous = e.NewCachedLabel();

      Xbyak::Label& fixup_label =
          e.AddToTail([&backtous](X64Emitter& e, Xbyak::Label& our_tail_label) {
            e.L(our_tail_label);

            Do0x1000Add(e, e.ecx);

            e.jmp(backtous, e.T_NEAR);
          });
      e.jae(fixup_label, e.T_NEAR);
      e.L(backtous);
    } else {
      e.mov(e.ecx, i.src1.reg().cvt32());
    }
    e.lock();
    e.cmpxchg(e.dword[e.GetMembaseReg() + e.rcx], i.src3);
    e.sete(i.dest);
  }
};
struct ATOMIC_COMPARE_EXCHANGE_I64
    : Sequence<ATOMIC_COMPARE_EXCHANGE_I64,
               I<OPCODE_ATOMIC_COMPARE_EXCHANGE, I8Op, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(e.rax, i.src2);
    if (xe::memory::allocation_granularity() > 0x1000) {
      // Emulate the 4 KB physical address offset in 0xE0000000+ when can't do
      // it via memory mapping.
      e.mov(e.ecx, i.src1.reg().cvt32());
      e.cmp(i.src1.reg().cvt32(), e.GetContextReg().cvt32());
      Xbyak::Label& backtous = e.NewCachedLabel();

      Xbyak::Label& fixup_label =
          e.AddToTail([&backtous](X64Emitter& e, Xbyak::Label& our_tail_label) {
            e.L(our_tail_label);

            Do0x1000Add(e, e.ecx);

            e.jmp(backtous, e.T_NEAR);
          });
      e.jae(fixup_label, e.T_NEAR);
      e.L(backtous);
    } else {
      e.mov(e.ecx, i.src1.reg().cvt32());
    }
    e.lock();
    e.cmpxchg(e.qword[e.GetMembaseReg() + e.rcx], i.src3);
    e.sete(i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ATOMIC_COMPARE_EXCHANGE,
                     ATOMIC_COMPARE_EXCHANGE_I32, ATOMIC_COMPARE_EXCHANGE_I64);

// ============================================================================
// OPCODE_LOAD_LOCAL
// ============================================================================
// Note: all types are always aligned on the stack.
struct LOAD_LOCAL_I8
    : Sequence<LOAD_LOCAL_I8, I<OPCODE_LOAD_LOCAL, I8Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, e.byte[e.rsp + i.src1.constant()]);
    // e.TraceLoadI8(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
struct LOAD_LOCAL_I16
    : Sequence<LOAD_LOCAL_I16, I<OPCODE_LOAD_LOCAL, I16Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, e.word[e.rsp + i.src1.constant()]);
    // e.TraceLoadI16(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
struct LOAD_LOCAL_I32
    : Sequence<LOAD_LOCAL_I32, I<OPCODE_LOAD_LOCAL, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, e.dword[e.rsp + i.src1.constant()]);
    // e.TraceLoadI32(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
struct LOAD_LOCAL_I64
    : Sequence<LOAD_LOCAL_I64, I<OPCODE_LOAD_LOCAL, I64Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, e.qword[e.rsp + i.src1.constant()]);
    // e.TraceLoadI64(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
struct LOAD_LOCAL_F32
    : Sequence<LOAD_LOCAL_F32, I<OPCODE_LOAD_LOCAL, F32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovss(i.dest, e.dword[e.rsp + i.src1.constant()]);
    // e.TraceLoadF32(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
struct LOAD_LOCAL_F64
    : Sequence<LOAD_LOCAL_F64, I<OPCODE_LOAD_LOCAL, F64Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovsd(i.dest, e.qword[e.rsp + i.src1.constant()]);
    // e.TraceLoadF64(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
struct LOAD_LOCAL_V128
    : Sequence<LOAD_LOCAL_V128, I<OPCODE_LOAD_LOCAL, V128Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovaps(i.dest, e.ptr[e.rsp + i.src1.constant()]);
    // e.TraceLoadV128(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_LOCAL, LOAD_LOCAL_I8, LOAD_LOCAL_I16,
                     LOAD_LOCAL_I32, LOAD_LOCAL_I64, LOAD_LOCAL_F32,
                     LOAD_LOCAL_F64, LOAD_LOCAL_V128);

// ============================================================================
// OPCODE_STORE_LOCAL
// ============================================================================
// Note: all types are always aligned on the stack.
struct STORE_LOCAL_I8
    : Sequence<STORE_LOCAL_I8, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreI8(DATA_LOCAL, i.src1.constant, i.src2);
    e.mov(e.byte[e.rsp + i.src1.constant()], i.src2);
  }
};

template <typename T>
static bool LocalStoreMayUseMembaseLow(X64Emitter& e, const T& i) {
  return i.src2.is_constant && i.src2.constant() == 0 &&
         e.CanUseMembaseLow32As0();
}
struct STORE_LOCAL_I16
    : Sequence<STORE_LOCAL_I16, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreI16(DATA_LOCAL, i.src1.constant, i.src2);
    if (LocalStoreMayUseMembaseLow(e, i)) {
      e.mov(e.word[e.rsp + i.src1.constant()], e.GetMembaseReg().cvt16());
    } else {
      e.mov(e.word[e.rsp + i.src1.constant()], i.src2);
    }
  }
};
struct STORE_LOCAL_I32
    : Sequence<STORE_LOCAL_I32, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreI32(DATA_LOCAL, i.src1.constant, i.src2);
    if (LocalStoreMayUseMembaseLow(e, i)) {
      e.mov(e.dword[e.rsp + i.src1.constant()], e.GetMembaseReg().cvt32());
    } else {
      e.mov(e.dword[e.rsp + i.src1.constant()], i.src2);
    }
  }
};
struct STORE_LOCAL_I64
    : Sequence<STORE_LOCAL_I64, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreI64(DATA_LOCAL, i.src1.constant, i.src2);
    if (i.src2.is_constant && i.src2.constant() == 0) {
      e.xor_(e.eax, e.eax);
      e.mov(e.qword[e.rsp + i.src1.constant()], e.rax);
    } else {
      e.mov(e.qword[e.rsp + i.src1.constant()], i.src2);
    }
  }
};
struct STORE_LOCAL_F32
    : Sequence<STORE_LOCAL_F32, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreF32(DATA_LOCAL, i.src1.constant, i.src2);
    e.vmovss(e.dword[e.rsp + i.src1.constant()], i.src2);
  }
};
struct STORE_LOCAL_F64
    : Sequence<STORE_LOCAL_F64, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreF64(DATA_LOCAL, i.src1.constant, i.src2);
    e.vmovsd(e.qword[e.rsp + i.src1.constant()], i.src2);
  }
};
struct STORE_LOCAL_V128
    : Sequence<STORE_LOCAL_V128, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreV128(DATA_LOCAL, i.src1.constant, i.src2);
    e.vmovaps(e.ptr[e.rsp + i.src1.constant()], i.src2);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE_LOCAL, STORE_LOCAL_I8, STORE_LOCAL_I16,
                     STORE_LOCAL_I32, STORE_LOCAL_I64, STORE_LOCAL_F32,
                     STORE_LOCAL_F64, STORE_LOCAL_V128);

// ============================================================================
// OPCODE_LOAD_CONTEXT
// ============================================================================
struct LOAD_CONTEXT_I8
    : Sequence<LOAD_CONTEXT_I8, I<OPCODE_LOAD_CONTEXT, I8Op, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    e.mov(i.dest, e.byte[addr]);
    if (IsTracingData()) {
      e.mov(e.GetNativeParam(0), i.src1.value);
      e.mov(e.GetNativeParam(1), e.byte[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceContextLoadI8));
    }
  }
};
struct LOAD_CONTEXT_I16
    : Sequence<LOAD_CONTEXT_I16, I<OPCODE_LOAD_CONTEXT, I16Op, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    e.mov(i.dest, e.word[addr]);
    if (IsTracingData()) {
      e.mov(e.GetNativeParam(1), e.word[addr]);
      e.mov(e.GetNativeParam(0), i.src1.value);
      e.CallNative(reinterpret_cast<void*>(TraceContextLoadI16));
    }
  }
};
struct LOAD_CONTEXT_I32
    : Sequence<LOAD_CONTEXT_I32, I<OPCODE_LOAD_CONTEXT, I32Op, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    e.mov(i.dest, e.dword[addr]);
    if (IsTracingData()) {
      e.mov(e.GetNativeParam(1), e.dword[addr]);
      e.mov(e.GetNativeParam(0), i.src1.value);
      e.CallNative(reinterpret_cast<void*>(TraceContextLoadI32));
    }
  }
};
template <typename EmitArgType>
static bool HandleLMS64Binary(X64Emitter& e, const EmitArgType& i,
                              LoadModStoreContext& lms, Xbyak::RegExp& addr) {
  uint64_t other_const_val = 0;
  bool const_fits_in_insn = false;
  if (lms.other_const) {
    other_const_val = lms.other_const->u64;
    const_fits_in_insn = e.ConstantFitsIn32Reg(other_const_val);
  }

  /*
        this check is here because we currently cannot handle other variables
     with this
  */
  if (!lms.other_const && !lms.binary_uses_twice) {
    return false;
  }

  if (lms.op == OPCODE_ADD) {
    if (lms.other_const) {
      if (const_fits_in_insn) {
        if (other_const_val == 1 &&
            e.IsFeatureEnabled(kX64FlagsIndependentVars)) {
          e.inc(e.qword[addr]);
        } else {
          e.add(e.qword[addr], (uint32_t)other_const_val);
        }

      } else {
        e.mov(e.rax, other_const_val);
        e.add(e.qword[addr], e.rax);
      }
      return true;
    } else if (lms.binary_uses_twice) {
      // we're being added to ourselves, we are a multiply by 2

      e.shl(e.qword[addr], 1);
      return true;
    } else if (lms.binary_other) {
      return false;  // cannot handle other variables right now.
    }
  } else if (lms.op == OPCODE_SUB) {
    if (lms.other_index != 1) {
      return false;  // if we are the second operand, we cant combine memory
                     // access and operation
    }

    if (lms.other_const) {
      if (const_fits_in_insn) {
        if (other_const_val == 1 &&
            e.IsFeatureEnabled(kX64FlagsIndependentVars)) {
          e.dec(e.qword[addr]);
        } else {
          e.sub(e.qword[addr], (uint32_t)other_const_val);
        }

      } else {
        e.mov(e.rax, other_const_val);
        e.sub(e.qword[addr], e.rax);
      }
      return true;
    }

  } else if (lms.op == OPCODE_AND) {
    if (lms.other_const) {
      if (const_fits_in_insn) {
        e.and_(e.qword[addr], (uint32_t)other_const_val);
      } else {
        e.mov(e.rax, other_const_val);
        e.and_(e.qword[addr], e.rax);
      }
      return true;
    }
  } else if (lms.op == OPCODE_OR) {
    if (lms.other_const) {
      if (const_fits_in_insn) {
        e.or_(e.qword[addr], (uint32_t)other_const_val);
      } else {
        e.mov(e.rax, other_const_val);
        e.or_(e.qword[addr], e.rax);
      }
      return true;
    }
  } else if (lms.op == OPCODE_XOR) {
    if (lms.other_const) {
      if (const_fits_in_insn) {
        e.xor_(e.qword[addr], (uint32_t)other_const_val);
      } else {
        e.mov(e.rax, other_const_val);
        e.xor_(e.qword[addr], e.rax);
      }
      return true;
    }
  }

  return false;
}
template <typename EmitArgType>
static bool HandleLMS64Unary(X64Emitter& e, const EmitArgType& i,
                             LoadModStoreContext& lms, Xbyak::RegExp& addr) {
  Opcode op = lms.op;

  if (op == OPCODE_NOT) {
    e.not_(e.qword[addr]);
    return true;
  } else if (op == OPCODE_NEG) {
    e.neg(e.qword[addr]);
    return true;
  }

  return false;
}
struct LOAD_CONTEXT_I64
    : Sequence<LOAD_CONTEXT_I64, I<OPCODE_LOAD_CONTEXT, I64Op, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    LoadModStoreContext lms{};
    if (GetLoadModStoreContext(i.instr, &lms)) {
      if (lms.is_binary && HandleLMS64Binary(e, i, lms, addr)) {
        lms.Consume();
        return;
      } else if (lms.is_unary && HandleLMS64Unary(e, i, lms, addr)) {
        lms.Consume();
        return;
      }
    }

    e.mov(i.dest, e.qword[addr]);
    if (IsTracingData()) {
      e.mov(e.GetNativeParam(1), e.qword[addr]);
      e.mov(e.GetNativeParam(0), i.src1.value);
      e.CallNative(reinterpret_cast<void*>(TraceContextLoadI64));
    }
  }
};
struct LOAD_CONTEXT_F32
    : Sequence<LOAD_CONTEXT_F32, I<OPCODE_LOAD_CONTEXT, F32Op, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    e.vmovss(i.dest, e.dword[addr]);
    if (IsTracingData()) {
      e.lea(e.GetNativeParam(1), e.dword[addr]);
      e.mov(e.GetNativeParam(0), i.src1.value);
      e.CallNative(reinterpret_cast<void*>(TraceContextLoadF32));
    }
  }
};
struct LOAD_CONTEXT_F64
    : Sequence<LOAD_CONTEXT_F64, I<OPCODE_LOAD_CONTEXT, F64Op, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    e.vmovsd(i.dest, e.qword[addr]);
    if (IsTracingData()) {
      e.lea(e.GetNativeParam(1), e.qword[addr]);
      e.mov(e.GetNativeParam(0), i.src1.value);
      e.CallNative(reinterpret_cast<void*>(TraceContextLoadF64));
    }
  }
};
struct LOAD_CONTEXT_V128
    : Sequence<LOAD_CONTEXT_V128, I<OPCODE_LOAD_CONTEXT, V128Op, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    e.vmovaps(i.dest, e.ptr[addr]);
    if (IsTracingData()) {
      e.lea(e.GetNativeParam(1), e.ptr[addr]);
      e.mov(e.GetNativeParam(0), i.src1.value);
      e.CallNative(reinterpret_cast<void*>(TraceContextLoadV128));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_CONTEXT, LOAD_CONTEXT_I8, LOAD_CONTEXT_I16,
                     LOAD_CONTEXT_I32, LOAD_CONTEXT_I64, LOAD_CONTEXT_F32,
                     LOAD_CONTEXT_F64, LOAD_CONTEXT_V128);

// ============================================================================
// OPCODE_STORE_CONTEXT
// ============================================================================
// Note: all types are always aligned on the stack.
struct STORE_CONTEXT_I8
    : Sequence<STORE_CONTEXT_I8,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.mov(e.byte[addr], i.src2.constant());
    } else {
      e.mov(e.byte[addr], i.src2);
    }
    if (IsTracingData()) {
      e.mov(e.GetNativeParam(1), e.byte[addr]);
      e.mov(e.GetNativeParam(0), i.src1.value);
      e.CallNative(reinterpret_cast<void*>(TraceContextStoreI8));
    }
  }
};
struct STORE_CONTEXT_I16
    : Sequence<STORE_CONTEXT_I16,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    if (i.src2.is_constant) {
      if (i.src2.constant() == 0 && e.CanUseMembaseLow32As0()) {
        e.mov(e.word[addr], e.GetMembaseReg().cvt16());
      } else {
        e.mov(e.word[addr], i.src2.constant());
      }
    } else {
      e.mov(e.word[addr], i.src2);
    }
    if (IsTracingData()) {
      e.mov(e.GetNativeParam(1), e.word[addr]);
      e.mov(e.GetNativeParam(0), i.src1.value);
      e.CallNative(reinterpret_cast<void*>(TraceContextStoreI16));
    }
  }
};
struct STORE_CONTEXT_I32
    : Sequence<STORE_CONTEXT_I32,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    if (i.src2.is_constant) {
      if (i.src2.constant() == 0 && e.CanUseMembaseLow32As0()) {
        e.mov(e.dword[addr], e.GetMembaseReg().cvt32());
      } else {
        e.mov(e.dword[addr], i.src2.constant());
      }
    } else {
      e.mov(e.dword[addr], i.src2);
    }
    if (IsTracingData()) {
      e.mov(e.GetNativeParam(1), e.dword[addr]);
      e.mov(e.GetNativeParam(0), i.src1.value);
      e.CallNative(reinterpret_cast<void*>(TraceContextStoreI32));
    }
  }
};
struct STORE_CONTEXT_I64
    : Sequence<STORE_CONTEXT_I64,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.MovMem64(addr, i.src2.constant());
    } else {
      e.mov(e.qword[addr], i.src2);
    }
    if (IsTracingData()) {
      e.mov(e.GetNativeParam(1), e.qword[addr]);
      e.mov(e.GetNativeParam(0), i.src1.value);
      e.CallNative(reinterpret_cast<void*>(TraceContextStoreI64));
    }
  }
};
struct STORE_CONTEXT_F32
    : Sequence<STORE_CONTEXT_F32,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.mov(e.dword[addr], i.src2.value->constant.i32);
    } else {
      e.vmovss(e.dword[addr], i.src2);
    }
    if (IsTracingData()) {
      e.lea(e.GetNativeParam(1), e.dword[addr]);
      e.mov(e.GetNativeParam(0), i.src1.value);
      e.CallNative(reinterpret_cast<void*>(TraceContextStoreF32));
    }
  }
};
struct STORE_CONTEXT_F64
    : Sequence<STORE_CONTEXT_F64,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.MovMem64(addr, i.src2.value->constant.i64);
    } else {
      e.vmovsd(e.qword[addr], i.src2);
    }
    if (IsTracingData()) {
      e.lea(e.GetNativeParam(1), e.qword[addr]);
      e.mov(e.GetNativeParam(0), i.src1.value);
      e.CallNative(reinterpret_cast<void*>(TraceContextStoreF64));
    }
  }
};
struct STORE_CONTEXT_V128
    : Sequence<STORE_CONTEXT_V128,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      e.vmovdqa(e.ptr[addr], e.xmm0);
    } else {
      SimdDomain domain = e.DeduceSimdDomain(i.src2.value);
      if (domain == SimdDomain::FLOATING) {
        e.vmovaps(e.ptr[addr], i.src2);
      } else {
        e.vmovdqa(e.ptr[addr], i.src2);
      }
    }
    if (IsTracingData()) {
      e.lea(e.GetNativeParam(1), e.ptr[addr]);
      e.mov(e.GetNativeParam(0), i.src1.value);
      e.CallNative(reinterpret_cast<void*>(TraceContextStoreV128));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE_CONTEXT, STORE_CONTEXT_I8, STORE_CONTEXT_I16,
                     STORE_CONTEXT_I32, STORE_CONTEXT_I64, STORE_CONTEXT_F32,
                     STORE_CONTEXT_F64, STORE_CONTEXT_V128);

// ============================================================================
// OPCODE_LOAD_MMIO
// ============================================================================
// Note: all types are always aligned in the context.
struct LOAD_MMIO_I32
    : Sequence<LOAD_MMIO_I32, I<OPCODE_LOAD_MMIO, I32Op, OffsetOp, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // uint64_t (context, addr)
    auto mmio_range = reinterpret_cast<MMIORange*>(i.src1.value);
    auto read_address = uint32_t(i.src2.value);
    e.mov(e.GetNativeParam(0), uint64_t(mmio_range->callback_context));
    e.mov(e.GetNativeParam(1).cvt32(), read_address);
    e.CallNativeSafe(reinterpret_cast<void*>(mmio_range->read));
    e.bswap(e.eax);
    e.mov(i.dest, e.eax);
    if (IsTracingData()) {
      e.mov(e.GetNativeParam(0), i.dest);
      e.mov(e.edx, read_address);
      e.CallNative(reinterpret_cast<void*>(TraceContextLoadI32));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_MMIO, LOAD_MMIO_I32);

// ============================================================================
// OPCODE_STORE_MMIO
// ============================================================================
// Note: all types are always aligned on the stack.
struct STORE_MMIO_I32
    : Sequence<STORE_MMIO_I32,
               I<OPCODE_STORE_MMIO, VoidOp, OffsetOp, OffsetOp, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // void (context, addr, value)
    auto mmio_range = reinterpret_cast<MMIORange*>(i.src1.value);
    auto write_address = uint32_t(i.src2.value);
    e.mov(e.GetNativeParam(0), uint64_t(mmio_range->callback_context));
    e.mov(e.GetNativeParam(1).cvt32(), write_address);
    if (i.src3.is_constant) {
      e.mov(e.GetNativeParam(2).cvt32(), xe::byte_swap(i.src3.constant()));
    } else {
      e.mov(e.GetNativeParam(2).cvt32(), i.src3);
      e.bswap(e.GetNativeParam(2).cvt32());
    }
    e.CallNativeSafe(reinterpret_cast<void*>(mmio_range->write));
    if (IsTracingData()) {
      if (i.src3.is_constant) {
        e.mov(e.GetNativeParam(0).cvt32(), i.src3.constant());
      } else {
        e.mov(e.GetNativeParam(0).cvt32(), i.src3);
      }
      e.mov(e.edx, write_address);
      e.CallNative(reinterpret_cast<void*>(TraceContextStoreI32));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE_MMIO, STORE_MMIO_I32);
// according to triangle we dont support mmio reads atm so no point in
// implementing this for them
static bool IsPossibleMMIOInstruction(X64Emitter& e, const hir::Instr* i) {
  if (!cvars::emit_mmio_aware_stores_for_recorded_exception_addresses) {
    return false;
  }
  if (IsTracingData()) {  // incompatible with tracing
    return false;
  }
  uint32_t guestaddr = i->GuestAddressFor();
  if (!guestaddr) {
    return false;
  }

  auto flags = e.GuestModule()->GetInstructionAddressFlags(guestaddr);

  return flags && flags->accessed_mmio;
}
template <typename T, bool swap>
static void MMIOAwareStore(void* _ctx, unsigned int guestaddr, T value) {
  if (swap) {
    value = xe::byte_swap(value);
  }
  if (guestaddr >= 0xE0000000) {
    guestaddr += 0x1000;
  }

  auto ctx = reinterpret_cast<ppc::PPCContext*>(_ctx);

  auto gaddr = ctx->processor->memory()->LookupVirtualMappedRange(guestaddr);
  if (!gaddr) {
    *reinterpret_cast<T*>(ctx->virtual_membase + guestaddr) = value;
  } else {
    value = xe::byte_swap(value); /*
          was having issues, found by comparing the values used with exceptions
          to these that we were reversed...
    */
    gaddr->write(nullptr, gaddr->callback_context, guestaddr, value);
  }
}

template <typename T, bool swap>
static T MMIOAwareLoad(void* _ctx, unsigned int guestaddr) {
  T value;

  if (guestaddr >= 0xE0000000) {
    guestaddr += 0x1000;
  }

  auto ctx = reinterpret_cast<ppc::PPCContext*>(_ctx);

  auto gaddr = ctx->processor->memory()->LookupVirtualMappedRange(guestaddr);
  if (!gaddr) {
    value = *reinterpret_cast<T*>(ctx->virtual_membase + guestaddr);
    if (swap) {
      value = xe::byte_swap(value);
    }
  } else {
    /*
          was having issues, found by comparing the values used with exceptions
          to these that we were reversed...
    */
    value = gaddr->read(nullptr, gaddr->callback_context, guestaddr);
  }
  return value;
}
// ============================================================================
// OPCODE_LOAD_OFFSET
// ============================================================================
struct LOAD_OFFSET_I8
    : Sequence<LOAD_OFFSET_I8, I<OPCODE_LOAD_OFFSET, I8Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddressOffset(e, i.src1, i.src2);
    e.mov(i.dest, e.byte[addr]);
  }
};

struct LOAD_OFFSET_I16
    : Sequence<LOAD_OFFSET_I16, I<OPCODE_LOAD_OFFSET, I16Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddressOffset(e, i.src1, i.src2);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      if (e.IsFeatureEnabled(kX64EmitMovbe)) {
        e.movbe(i.dest, e.word[addr]);
      } else {
        e.mov(i.dest, e.word[addr]);
        e.ror(i.dest, 8);
      }
    } else {
      e.mov(i.dest, e.word[addr]);
    }
  }
};

struct LOAD_OFFSET_I32
    : Sequence<LOAD_OFFSET_I32, I<OPCODE_LOAD_OFFSET, I32Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (IsPossibleMMIOInstruction(e, i.instr)) {
      void* addrptr = (void*)&MMIOAwareLoad<uint32_t, false>;

      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        addrptr = (void*)&MMIOAwareLoad<uint32_t, true>;
      }
      if (i.src1.is_constant) {
        e.mov(e.GetNativeParam(0).cvt32(), (uint32_t)i.src1.constant());
      } else {
        e.mov(e.GetNativeParam(0).cvt32(), i.src1.reg().cvt32());
      }

      if (i.src2.is_constant) {
        e.add(e.GetNativeParam(0).cvt32(), (uint32_t)i.src2.constant());
      } else {
        e.add(e.GetNativeParam(0).cvt32(), i.src2.reg().cvt32());
      }

      e.CallNativeSafe(addrptr);
      e.mov(i.dest, e.eax);
    } else {
      auto addr = ComputeMemoryAddressOffset(e, i.src1, i.src2);
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        if (e.IsFeatureEnabled(kX64EmitMovbe)) {
          e.movbe(i.dest, e.dword[addr]);
        } else {
          e.mov(i.dest, e.dword[addr]);
          e.bswap(i.dest);
        }
      } else {
        e.mov(i.dest, e.dword[addr]);
      }
    }
  }
};

struct LOAD_OFFSET_I64
    : Sequence<LOAD_OFFSET_I64, I<OPCODE_LOAD_OFFSET, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddressOffset(e, i.src1, i.src2);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      if (e.IsFeatureEnabled(kX64EmitMovbe)) {
        e.movbe(i.dest, e.qword[addr]);
      } else {
        e.mov(i.dest, e.qword[addr]);
        e.bswap(i.dest);
      }
    } else {
      e.mov(i.dest, e.qword[addr]);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_OFFSET, LOAD_OFFSET_I8, LOAD_OFFSET_I16,
                     LOAD_OFFSET_I32, LOAD_OFFSET_I64);

// ============================================================================
// OPCODE_STORE_OFFSET
// ============================================================================
struct STORE_OFFSET_I8
    : Sequence<STORE_OFFSET_I8,
               I<OPCODE_STORE_OFFSET, VoidOp, I64Op, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddressOffset(e, i.src1, i.src2);

    if (i.src3.is_constant) {
      e.mov(e.byte[addr], i.src3.constant());
    } else {
      e.mov(e.byte[addr], i.src3);
    }
  }
};

struct STORE_OFFSET_I16
    : Sequence<STORE_OFFSET_I16,
               I<OPCODE_STORE_OFFSET, VoidOp, I64Op, I64Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddressOffset(e, i.src1, i.src2);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_false(i.src3.is_constant);
      if (e.IsFeatureEnabled(kX64EmitMovbe)) {
        e.movbe(e.word[addr], i.src3);
      } else {
        assert_always("not implemented");
      }
    } else {
      if (i.src3.is_constant) {
        if (i.src3.constant() == 0 && e.CanUseMembaseLow32As0()) {
          e.mov(e.word[addr], e.GetMembaseReg().cvt16());
        } else {
          e.mov(e.word[addr], i.src3.constant());
        }
      } else {
        e.mov(e.word[addr], i.src3);
      }
    }
  }
};

struct STORE_OFFSET_I32
    : Sequence<STORE_OFFSET_I32,
               I<OPCODE_STORE_OFFSET, VoidOp, I64Op, I64Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (IsPossibleMMIOInstruction(e, i.instr)) {
      void* addrptr = (void*)&MMIOAwareStore<uint32_t, false>;

      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        addrptr = (void*)&MMIOAwareStore<uint32_t, true>;
      }
      if (i.src1.is_constant) {
        e.mov(e.GetNativeParam(0).cvt32(), i.src1.constant());
      } else {
        e.mov(e.GetNativeParam(0).cvt32(), i.src1.reg().cvt32());
      }
      if (i.src2.is_constant) {
        e.add(e.GetNativeParam(0).cvt32(), (uint32_t)i.src2.constant());
      } else {
        e.add(e.GetNativeParam(0).cvt32(), i.src2);
      }
      if (i.src3.is_constant) {
        e.mov(e.GetNativeParam(1).cvt32(), i.src3.constant());
      } else {
        e.mov(e.GetNativeParam(1).cvt32(), i.src3);
      }
      e.CallNativeSafe(addrptr);

    } else {
      auto addr = ComputeMemoryAddressOffset(e, i.src1, i.src2);
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        assert_false(i.src3.is_constant);
        if (e.IsFeatureEnabled(kX64EmitMovbe)) {
          e.movbe(e.dword[addr], i.src3);
        } else {
          assert_always("not implemented");
        }
      } else {
        if (i.src3.is_constant) {
          if (i.src3.constant() == 0 && e.CanUseMembaseLow32As0()) {
            e.mov(e.dword[addr], e.GetMembaseReg().cvt32());
          } else {
            e.mov(e.dword[addr], i.src3.constant());
          }
        } else {
          e.mov(e.dword[addr], i.src3);
        }
      }
    }
  }
};

struct STORE_OFFSET_I64
    : Sequence<STORE_OFFSET_I64,
               I<OPCODE_STORE_OFFSET, VoidOp, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddressOffset(e, i.src1, i.src2);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_false(i.src3.is_constant);
      if (e.IsFeatureEnabled(kX64EmitMovbe)) {
        e.movbe(e.qword[addr], i.src3);
      } else {
        assert_always("not implemented");
      }
    } else {
      if (i.src3.is_constant) {
        e.MovMem64(addr, i.src3.constant());
      } else {
        e.mov(e.qword[addr], i.src3);
      }
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE_OFFSET, STORE_OFFSET_I8, STORE_OFFSET_I16,
                     STORE_OFFSET_I32, STORE_OFFSET_I64);

// ============================================================================
// OPCODE_LOAD
// ============================================================================
struct LOAD_I8 : Sequence<LOAD_I8, I<OPCODE_LOAD, I8Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.mov(i.dest, e.byte[addr]);
    if (IsTracingData()) {
      e.mov(e.GetNativeParam(1).cvt8(), i.dest);
      e.lea(e.GetNativeParam(0), e.ptr[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceMemoryLoadI8));
    }
  }
};
struct LOAD_I16 : Sequence<LOAD_I16, I<OPCODE_LOAD, I16Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      if (e.IsFeatureEnabled(kX64EmitMovbe)) {
        e.movbe(i.dest, e.word[addr]);
      } else {
        e.mov(i.dest, e.word[addr]);
        e.ror(i.dest, 8);
      }
    } else {
      e.mov(i.dest, e.word[addr]);
    }
    if (IsTracingData()) {
      e.mov(e.GetNativeParam(1).cvt16(), i.dest);
      e.lea(e.GetNativeParam(0), e.ptr[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceMemoryLoadI16));
    }
  }
};
struct LOAD_I32 : Sequence<LOAD_I32, I<OPCODE_LOAD, I32Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (IsPossibleMMIOInstruction(e, i.instr)) {
      void* addrptr = (void*)&MMIOAwareLoad<uint32_t, false>;

      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        addrptr = (void*)&MMIOAwareLoad<uint32_t, true>;
      }
      if (i.src1.is_constant) {
        e.mov(e.GetNativeParam(0).cvt32(), (uint32_t)i.src1.constant());
      } else {
        e.mov(e.GetNativeParam(0).cvt32(), i.src1.reg().cvt32());
      }

      e.CallNativeSafe(addrptr);
      e.mov(i.dest, e.eax);
    } else {
      auto addr = ComputeMemoryAddress(e, i.src1);
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        if (e.IsFeatureEnabled(kX64EmitMovbe)) {
          e.movbe(i.dest, e.dword[addr]);
        } else {
          e.mov(i.dest, e.dword[addr]);
          e.bswap(i.dest);
        }
      } else {
        e.mov(i.dest, e.dword[addr]);
      }
      if (IsTracingData()) {
        e.mov(e.GetNativeParam(1).cvt32(), i.dest);
        e.lea(e.GetNativeParam(0), e.ptr[addr]);
        e.CallNative(reinterpret_cast<void*>(TraceMemoryLoadI32));
      }
    }
  }
};
struct LOAD_I64 : Sequence<LOAD_I64, I<OPCODE_LOAD, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      if (e.IsFeatureEnabled(kX64EmitMovbe)) {
        e.movbe(i.dest, e.qword[addr]);
      } else {
        e.mov(i.dest, e.qword[addr]);
        e.bswap(i.dest);
      }
    } else {
      e.mov(i.dest, e.qword[addr]);
    }
    if (IsTracingData()) {
      e.mov(e.GetNativeParam(1), i.dest);
      e.lea(e.GetNativeParam(0), e.ptr[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceMemoryLoadI64));
    }
  }
};
struct LOAD_F32 : Sequence<LOAD_F32, I<OPCODE_LOAD, F32Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.vmovss(i.dest, e.dword[addr]);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_always("not implemented yet");
    }
    if (IsTracingData()) {
      e.lea(e.GetNativeParam(1), e.dword[addr]);
      e.lea(e.GetNativeParam(0), e.ptr[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceMemoryLoadF32));
    }
  }
};
struct LOAD_F64 : Sequence<LOAD_F64, I<OPCODE_LOAD, F64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.vmovsd(i.dest, e.qword[addr]);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_always("not implemented yet");
    }
    if (IsTracingData()) {
      e.lea(e.GetNativeParam(1), e.qword[addr]);
      e.lea(e.GetNativeParam(0), e.ptr[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceMemoryLoadF64));
    }
  }
};
struct LOAD_V128 : Sequence<LOAD_V128, I<OPCODE_LOAD, V128Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    // TODO(benvanik): we should try to stick to movaps if possible.
    e.vmovdqa(i.dest, e.ptr[addr]);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      // TODO(benvanik): find a way to do this without the memory load.
      e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMByteSwapMask));
    }
    if (IsTracingData()) {
      e.lea(e.GetNativeParam(1), e.ptr[addr]);
      e.lea(e.GetNativeParam(0), e.ptr[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceMemoryLoadV128));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD, LOAD_I8, LOAD_I16, LOAD_I32, LOAD_I64,
                     LOAD_F32, LOAD_F64, LOAD_V128);

// ============================================================================
// OPCODE_STORE
// ============================================================================
// Note: most *should* be aligned, but needs to be checked!
struct STORE_I8 : Sequence<STORE_I8, I<OPCODE_STORE, VoidOp, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.mov(e.byte[addr], i.src2.constant());
    } else {
      e.mov(e.byte[addr], i.src2);
    }
    if (IsTracingData()) {
      addr = ComputeMemoryAddress(e, i.src1);
      e.mov(e.GetNativeParam(1).cvt8(), e.byte[addr]);
      e.lea(e.GetNativeParam(0), e.ptr[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceMemoryStoreI8));
    }
  }
};
struct STORE_I16 : Sequence<STORE_I16, I<OPCODE_STORE, VoidOp, I64Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_false(i.src2.is_constant);
      if (e.IsFeatureEnabled(kX64EmitMovbe)) {
        e.movbe(e.word[addr], i.src2);
      } else {
        assert_always("not implemented");
      }
    } else {
      if (i.src2.is_constant) {
        e.mov(e.word[addr], i.src2.constant());
      } else {
        e.mov(e.word[addr], i.src2);
      }
    }
    if (IsTracingData()) {
      addr = ComputeMemoryAddress(e, i.src1);
      e.mov(e.GetNativeParam(1).cvt16(), e.word[addr]);
      e.lea(e.GetNativeParam(0), e.ptr[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceMemoryStoreI16));
    }
  }
};
struct STORE_I32 : Sequence<STORE_I32, I<OPCODE_STORE, VoidOp, I64Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (IsPossibleMMIOInstruction(e, i.instr)) {
      void* addrptr = (void*)&MMIOAwareStore<uint32_t, false>;

      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        addrptr = (void*)&MMIOAwareStore<uint32_t, true>;
      }
      if (i.src1.is_constant) {
        e.mov(e.GetNativeParam(0).cvt32(), (uint32_t)i.src1.constant());
      } else {
        e.mov(e.GetNativeParam(0).cvt32(), i.src1.reg().cvt32());
      }
      if (i.src2.is_constant) {
        e.mov(e.GetNativeParam(1).cvt32(), i.src2.constant());
      } else {
        e.mov(e.GetNativeParam(1).cvt32(), i.src2);
      }
      e.CallNativeSafe(addrptr);

    } else {
      auto addr = ComputeMemoryAddress(e, i.src1);
      if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
        assert_false(i.src2.is_constant);
        if (e.IsFeatureEnabled(kX64EmitMovbe)) {
          e.movbe(e.dword[addr], i.src2);
        } else {
          assert_always("not implemented");
        }
      } else {
        if (i.src2.is_constant) {
          e.mov(e.dword[addr], i.src2.constant());
        } else {
          e.mov(e.dword[addr], i.src2);
        }
        if (IsTracingData()) {
          e.mov(e.GetNativeParam(1).cvt32(), e.dword[addr]);
          e.lea(e.GetNativeParam(0), e.ptr[addr]);
          e.CallNative(reinterpret_cast<void*>(TraceMemoryStoreI32));
        }
      }
    }
  }
};
struct STORE_I64 : Sequence<STORE_I64, I<OPCODE_STORE, VoidOp, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_false(i.src2.is_constant);
      if (e.IsFeatureEnabled(kX64EmitMovbe)) {
        e.movbe(e.qword[addr], i.src2);
      } else {
        assert_always("not implemented");
      }
    } else {
      if (i.src2.is_constant) {
        e.MovMem64(addr, i.src2.constant());
      } else {
        e.mov(e.qword[addr], i.src2);
      }
    }
    if (IsTracingData()) {
      addr = ComputeMemoryAddress(e, i.src1);
      e.mov(e.GetNativeParam(1), e.qword[addr]);
      e.lea(e.GetNativeParam(0), e.ptr[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceMemoryStoreI64));
    }
  }
};
struct STORE_F32 : Sequence<STORE_F32, I<OPCODE_STORE, VoidOp, I64Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_false(i.src2.is_constant);
      assert_always("not yet implemented");
    } else {
      if (i.src2.is_constant) {
        e.mov(e.dword[addr], i.src2.value->constant.i32);
      } else {
        e.vmovss(e.dword[addr], i.src2);
      }
    }
    if (IsTracingData()) {
      addr = ComputeMemoryAddress(e, i.src1);
      e.lea(e.GetNativeParam(1), e.ptr[addr]);
      e.lea(e.GetNativeParam(0), e.ptr[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceMemoryStoreF32));
    }
  }
};
struct STORE_F64 : Sequence<STORE_F64, I<OPCODE_STORE, VoidOp, I64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_false(i.src2.is_constant);
      assert_always("not yet implemented");
    } else {
      if (i.src2.is_constant) {
        e.MovMem64(addr, i.src2.value->constant.i64);
      } else {
        e.vmovsd(e.qword[addr], i.src2);
      }
    }
    if (IsTracingData()) {
      addr = ComputeMemoryAddress(e, i.src1);
      e.lea(e.GetNativeParam(1), e.ptr[addr]);
      e.lea(e.GetNativeParam(0), e.ptr[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceMemoryStoreF64));
    }
  }
};
struct STORE_V128
    : Sequence<STORE_V128, I<OPCODE_STORE, VoidOp, I64Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_false(i.src2.is_constant);
      e.vpshufb(e.xmm0, i.src2, e.GetXmmConstPtr(XMMByteSwapMask));
      // changed from vmovaps, the penalty on the vpshufb is unavoidable but
      // we dont need to incur another here too
      e.vmovdqa(e.ptr[addr], e.xmm0);
    } else {
      if (i.src2.is_constant) {
        e.LoadConstantXmm(e.xmm0, i.src2.constant());
        e.vmovdqa(e.ptr[addr], e.xmm0);
      } else {
        e.vmovdqa(e.ptr[addr], i.src2);
      }
    }
    if (IsTracingData()) {
      addr = ComputeMemoryAddress(e, i.src1);
      e.lea(e.GetNativeParam(1), e.ptr[addr]);
      e.lea(e.GetNativeParam(0), e.ptr[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceMemoryStoreV128));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE, STORE_I8, STORE_I16, STORE_I32, STORE_I64,
                     STORE_F32, STORE_F64, STORE_V128);

// ============================================================================
// OPCODE_CACHE_CONTROL
// ============================================================================
struct CACHE_CONTROL
    : Sequence<CACHE_CONTROL,
               I<OPCODE_CACHE_CONTROL, VoidOp, I64Op, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    bool is_clflush = false, is_prefetch = false, is_prefetchw = false;
    switch (CacheControlType(i.instr->flags)) {
      case CacheControlType::CACHE_CONTROL_TYPE_DATA_TOUCH_FOR_STORE:
        is_prefetchw = true;
        break;
      case CacheControlType::CACHE_CONTROL_TYPE_DATA_TOUCH:
        is_prefetch = true;
        break;
      case CacheControlType::CACHE_CONTROL_TYPE_DATA_STORE:
      case CacheControlType::CACHE_CONTROL_TYPE_DATA_STORE_AND_FLUSH:
        is_clflush = true;
        break;
      default:
        assert_unhandled_case(CacheControlType(i.instr->flags));
        return;
    }
    if (is_prefetchw && !e.IsFeatureEnabled(kX64EmitPrefetchW)) {
      is_prefetchw = false;
      is_prefetch = true;  // cant prefetchw, cpu doesnt have it (unlikely to
                           // happen). just prefetcht0
    }
    size_t cache_line_size = i.src2.value;

    RegExp addr;
    uint32_t address_constant;
    if (i.src1.is_constant) {
      // TODO(benvanik): figure out how to do this without a temp.
      // Since the constant is often 0x8... if we tried to use that as a
      // displacement it would be sign extended and mess things up.
      address_constant = static_cast<uint32_t>(i.src1.constant());
      if (address_constant < 0x80000000) {
        addr = e.GetMembaseReg() + address_constant;
      } else {
        if (address_constant >= 0xE0000000 &&
            xe::memory::allocation_granularity() > 0x1000) {
          e.mov(e.eax, address_constant + 0x1000);
        } else {
          e.mov(e.eax, address_constant);
        }
        addr = e.GetMembaseReg() + e.rax;
      }
    } else {
      if (xe::memory::allocation_granularity() > 0x1000) {
        // Emulate the 4 KB physical address offset in 0xE0000000+ when can't
        // do it via memory mapping.
        e.mov(e.eax, i.src1.reg().cvt32());

        e.cmp(i.src1.reg().cvt32(), e.GetContextReg().cvt32());

        Xbyak::Label& tmplbl = e.NewCachedLabel();

        Xbyak::Label& fixup_label =
            e.AddToTail([&tmplbl](X64Emitter& e, Xbyak::Label& our_tail_label) {
              e.L(our_tail_label);

              Do0x1000Add(e, e.eax);

              e.jmp(tmplbl, e.T_NEAR);
            });
        e.jae(fixup_label, e.T_NEAR);
        e.L(tmplbl);
      } else {
        // Clear the top 32 bits, as they are likely garbage.
        // TODO(benvanik): find a way to avoid doing this.
        e.mov(e.eax, i.src1.reg().cvt32());
      }
      addr = e.GetMembaseReg() + e.rax;
    }
    // todo: use clflushopt + sfence on cpus that support it
    if (is_clflush) {
      e.clflush(e.ptr[addr]);
    }

    if (is_prefetch) {
      e.prefetcht0(e.ptr[addr]);
    }
    if (is_prefetchw) {
      e.prefetchw(e.ptr[addr]);
    }

    if (cache_line_size >= 128) {
      // Prefetch the other 64 bytes of the 128-byte cache line.
      if (i.src1.is_constant && address_constant < 0x80000000) {
        addr = e.GetMembaseReg() + (address_constant ^ 64);
      } else {
        e.xor_(e.eax, 64);
      }
      if (is_clflush) {
        e.clflush(e.ptr[addr]);
      }
      if (is_prefetch) {
        e.prefetcht0(e.ptr[addr]);
      }
      if (is_prefetchw) {
        e.prefetchw(e.ptr[addr]);
      }
      assert_true(cache_line_size == 128);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CACHE_CONTROL, CACHE_CONTROL);

// ============================================================================
// OPCODE_MEMORY_BARRIER
// ============================================================================
struct MEMORY_BARRIER
    : Sequence<MEMORY_BARRIER, I<OPCODE_MEMORY_BARRIER, VoidOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) { e.mfence(); }
};
EMITTER_OPCODE_TABLE(OPCODE_MEMORY_BARRIER, MEMORY_BARRIER);

// ============================================================================
// OPCODE_MEMSET
// ============================================================================
struct MEMSET_I64_I8_I64
    : Sequence<MEMSET_I64_I8_I64,
               I<OPCODE_MEMSET, VoidOp, I64Op, I8Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    assert_true(i.src3.is_constant);
    assert_true(i.src2.constant() == 0);
    e.vpxor(e.xmm0, e.xmm0);
    auto addr = ComputeMemoryAddress(e, i.src1);
    /*
        chrispy: changed to vmovdqa, the mismatch between vpxor and vmovaps
       was causing a 1 cycle stall before the first store
    */
    switch (i.src3.constant()) {
      case 32:

        e.vmovdqa(e.ptr[addr], e.ymm0);
        break;
      case 128:
        // probably should lea the address beforehand
        e.vmovdqa(e.ptr[addr + 0 * 16], e.ymm0);

        e.vmovdqa(e.ptr[addr + 2 * 16], e.ymm0);

        e.vmovdqa(e.ptr[addr + 4 * 16], e.ymm0);

        e.vmovdqa(e.ptr[addr + 6 * 16], e.ymm0);
        break;
      default:
        assert_unhandled_case(i.src3.constant());
        break;
    }
    if (IsTracingData()) {
      addr = ComputeMemoryAddress(e, i.src1);
      e.mov(e.GetNativeParam(2), i.src3.constant());
      e.mov(e.GetNativeParam(1), i.src2.constant());
      e.lea(e.GetNativeParam(0), e.ptr[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceMemset));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MEMSET, MEMSET_I64_I8_I64);

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
