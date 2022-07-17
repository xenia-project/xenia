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
#include "xenia/cpu/backend/x64/x64_op.h"
#include "xenia/cpu/backend/x64/x64_tracers.h"
#include "xenia/cpu/ppc/ppc_context.h"

DEFINE_bool(
    elide_e0_check, false,
    "Eliminate e0 check on some memory accesses, like to r13(tls) or r1(sp)",
    "CPU");

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

volatile int anchor_memory = 0;

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
template <typename T>
RegExp ComputeMemoryAddressOffset(X64Emitter& e, const T& guest,
                                  const T& offset) {
  assert_true(offset.is_constant);
  int32_t offset_const = static_cast<int32_t>(offset.constant());

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
      e.xor_(e.eax, e.eax);
      e.lea(e.edx, e.ptr[guest.reg().cvt32() + offset_const]);

      e.cmp(e.edx, e.GetContextReg().cvt32());
      e.setae(e.al);
      e.shl(e.eax, 12);
      e.add(e.eax, e.edx);
      return e.GetMembaseReg() + e.rax;

    } else {
      // Clear the top 32 bits, as they are likely garbage.
      // TODO(benvanik): find a way to avoid doing this.

      e.mov(e.eax, guest.reg().cvt32());
    }
    return e.GetMembaseReg() + e.rax + offset_const;
  }
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
      e.xor_(e.eax, e.eax);
      e.cmp(guest.reg().cvt32(), e.GetContextReg().cvt32());
      e.setae(e.al);
      e.shl(e.eax, 12);
      e.add(e.eax, guest.reg().cvt32());
    } else {
      // Clear the top 32 bits, as they are likely garbage.
      // TODO(benvanik): find a way to avoid doing this.
      e.mov(e.eax, guest.reg().cvt32());
    }
    return e.GetMembaseReg() + e.rax;
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
      e.cmp(i.src1.reg().cvt32(), e.GetContextReg().cvt32());
      e.setae(e.cl);
      e.movzx(e.ecx, e.cl);
      e.shl(e.ecx, 12);
      e.add(e.ecx, i.src1.reg().cvt32());
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
      e.cmp(i.src1.reg().cvt32(), e.GetContextReg().cvt32());
      e.setae(e.cl);
      e.movzx(e.ecx, e.cl);
      e.shl(e.ecx, 12);
      e.add(e.ecx, i.src1.reg().cvt32());
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
struct STORE_LOCAL_I16
    : Sequence<STORE_LOCAL_I16, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreI16(DATA_LOCAL, i.src1.constant, i.src2);
    e.mov(e.word[e.rsp + i.src1.constant()], i.src2);
  }
};
struct STORE_LOCAL_I32
    : Sequence<STORE_LOCAL_I32, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreI32(DATA_LOCAL, i.src1.constant, i.src2);
    e.mov(e.dword[e.rsp + i.src1.constant()], i.src2);
  }
};
struct STORE_LOCAL_I64
    : Sequence<STORE_LOCAL_I64, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreI64(DATA_LOCAL, i.src1.constant, i.src2);
    e.mov(e.qword[e.rsp + i.src1.constant()], i.src2);
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
struct LOAD_CONTEXT_I64
    : Sequence<LOAD_CONTEXT_I64, I<OPCODE_LOAD_CONTEXT, I64Op, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
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
      e.mov(e.word[addr], i.src2.constant());
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
      e.mov(e.dword[addr], i.src2.constant());
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
      e.vmovaps(e.ptr[addr], e.xmm0);
    } else {
      e.vmovaps(e.ptr[addr], i.src2);
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
        e.mov(e.word[addr], i.src3.constant());
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
        e.mov(e.dword[addr], i.src3.constant());
      } else {
        e.mov(e.dword[addr], i.src3);
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
    e.vmovups(i.dest, e.ptr[addr]);
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
    }
    if (IsTracingData()) {
      addr = ComputeMemoryAddress(e, i.src1);
      e.mov(e.GetNativeParam(1).cvt32(), e.dword[addr]);
      e.lea(e.GetNativeParam(0), e.ptr[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceMemoryStoreI32));
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
      e.vmovaps(e.ptr[addr], e.xmm0);
    } else {
      if (i.src2.is_constant) {
        e.LoadConstantXmm(e.xmm0, i.src2.constant());
        e.vmovaps(e.ptr[addr], e.xmm0);
      } else {
        e.vmovaps(e.ptr[addr], i.src2);
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
    bool is_clflush = false, is_prefetch = false;
    switch (CacheControlType(i.instr->flags)) {
      case CacheControlType::CACHE_CONTROL_TYPE_DATA_TOUCH:
      case CacheControlType::CACHE_CONTROL_TYPE_DATA_TOUCH_FOR_STORE:
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
        // Emulate the 4 KB physical address offset in 0xE0000000+ when can't do
        // it via memory mapping.
        e.cmp(i.src1.reg().cvt32(), e.GetContextReg().cvt32());
        e.setae(e.al);
        e.movzx(e.eax, e.al);
        e.shl(e.eax, 12);
        e.add(e.eax, i.src1.reg().cvt32());
      } else {
        // Clear the top 32 bits, as they are likely garbage.
        // TODO(benvanik): find a way to avoid doing this.
        e.mov(e.eax, i.src1.reg().cvt32());
      }
      addr = e.GetMembaseReg() + e.rax;
    }
    if (is_clflush) {
      e.clflush(e.ptr[addr]);
    }
    if (is_prefetch) {
      e.prefetcht0(e.ptr[addr]);
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
    switch (i.src3.constant()) {
      case 32:
        e.vmovaps(e.ptr[addr + 0 * 16], e.xmm0);
        e.vmovaps(e.ptr[addr + 1 * 16], e.xmm0);
        break;
      case 128:
        e.vmovaps(e.ptr[addr + 0 * 16], e.xmm0);
        e.vmovaps(e.ptr[addr + 1 * 16], e.xmm0);
        e.vmovaps(e.ptr[addr + 2 * 16], e.xmm0);
        e.vmovaps(e.ptr[addr + 3 * 16], e.xmm0);
        e.vmovaps(e.ptr[addr + 4 * 16], e.xmm0);
        e.vmovaps(e.ptr[addr + 5 * 16], e.xmm0);
        e.vmovaps(e.ptr[addr + 6 * 16], e.xmm0);
        e.vmovaps(e.ptr[addr + 7 * 16], e.xmm0);
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
