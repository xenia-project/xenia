/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Developers. All rights reserved.                      *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_sequences.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/memory.h"
#include "xenia/cpu/backend/a64/a64_op.h"
#include "xenia/cpu/backend/a64/a64_tracers.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

volatile int anchor_memory = 0;

template <typename T>
XReg ComputeMemoryAddressOffset(A64Emitter& e, const T& guest, const T& offset,
                                WReg address_register = W3) {
  assert_true(offset.is_constant);
  int32_t offset_const = static_cast<int32_t>(offset.constant());

  if (guest.is_constant) {
    uint32_t address = static_cast<uint32_t>(guest.constant());
    address += offset_const;
    if (address < 0x80000000) {
      e.MOV(address_register.toX(), address);
      e.ADD(address_register.toX(), e.GetMembaseReg(), address_register.toX());
      return address_register.toX();
    } else {
      if (address >= 0xE0000000 &&
          xe::memory::allocation_granularity() > 0x1000) {
        e.MOV(W0, address + 0x1000);
      } else {
        e.MOV(W0, address);
      }
      e.ADD(address_register.toX(), e.GetMembaseReg(), X0);
      return address_register.toX();
    }
  } else {
    if (xe::memory::allocation_granularity() > 0x1000) {
      // Emulate the 4 KB physical address offset in 0xE0000000+ when can't do
      // it via memory mapping.
      e.MOV(W0, 0xE0000000 - offset_const);
      e.CMP(guest.reg().toW(), W0);
      e.CSET(W0, Cond::HS);
      e.LSL(W0, W0, 12);
      e.ADD(W0, W0, guest.reg().toW());
    } else {
      // Clear the top 32 bits, as they are likely garbage.
      // TODO(benvanik): find a way to avoid doing this.
      e.MOV(W0, guest.reg().toW());
    }
    e.MOV(X1, offset_const);
    e.ADD(X0, X0, X1);

    e.ADD(address_register.toX(), e.GetMembaseReg(), X0);
    return address_register.toX();
  }
}

// Note: most *should* be aligned, but needs to be checked!
template <typename T>
XReg ComputeMemoryAddress(A64Emitter& e, const T& guest,
                          WReg address_register = W3) {
  if (guest.is_constant) {
    // TODO(benvanik): figure out how to do this without a temp.
    // Since the constant is often 0x8... if we tried to use that as a
    // displacement it would be sign extended and mess things up.
    uint32_t address = static_cast<uint32_t>(guest.constant());
    if (address < 0x80000000) {
      e.ADD(address_register.toX(), e.GetMembaseReg(), address);
      return address_register.toX();
    } else {
      if (address >= 0xE0000000 &&
          xe::memory::allocation_granularity() > 0x1000) {
        e.MOV(W0, address + 0x1000u);
      } else {
        e.MOV(W0, address);
      }
      e.ADD(address_register.toX(), e.GetMembaseReg(), X0);
      return address_register.toX();
    }
  } else {
    if (xe::memory::allocation_granularity() > 0x1000) {
      // Emulate the 4 KB physical address offset in 0xE0000000+ when can't do
      // it via memory mapping.
      e.MOV(W0, 0xE0000000);
      e.CMP(guest.reg().toW(), W0);
      e.CSET(W0, Cond::HS);
      e.LSL(W0, W0, 12);
      e.ADD(W0, W0, guest.reg().toW());
    } else {
      // Clear the top 32 bits, as they are likely garbage.
      // TODO(benvanik): find a way to avoid doing this.
      e.MOV(W0, guest.reg().toW());
    }
    e.ADD(address_register.toX(), e.GetMembaseReg(), X0);
    return address_register.toX();
    // return e.GetMembaseReg() + e.rax;
  }
}

// ============================================================================
// OPCODE_ATOMIC_EXCHANGE
// ============================================================================
// Note that the address we use here is a real, host address!
// This is weird, and should be fixed.
template <typename SEQ, typename REG, typename ARGS, typename FN>
void EmitAtomicExchangeXX(A64Emitter& e, const ARGS& i, const FN& fn) {
  if (i.dest == i.src1) {
    e.MOV(X0, i.src1);
    if (i.dest != i.src2) {
      if (i.src2.is_constant) {
        e.MOV(i.dest, i.src2.constant());
      } else {
        e.MOV(i.dest, i.src2);
      }
    }
    fn(e, i.dest, X0);
  } else {
    if (i.dest != i.src2) {
      if (i.src2.is_constant) {
        e.MOV(i.dest, i.src2.constant());
      } else {
        e.MOV(i.dest, i.src2);
      }
    }
    fn(e, i.dest, i.src1);
  }
}
struct ATOMIC_EXCHANGE_I8
    : Sequence<ATOMIC_EXCHANGE_I8,
               I<OPCODE_ATOMIC_EXCHANGE, I8Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAtomicExchangeXX<ATOMIC_EXCHANGE_I8, WReg>(
        e, i,
        [](A64Emitter& e, WReg dest, XReg src) { e.SWPALB(dest, dest, src); });
  }
};
struct ATOMIC_EXCHANGE_I16
    : Sequence<ATOMIC_EXCHANGE_I16,
               I<OPCODE_ATOMIC_EXCHANGE, I16Op, I64Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAtomicExchangeXX<ATOMIC_EXCHANGE_I8, WReg>(
        e, i,
        [](A64Emitter& e, WReg dest, XReg src) { e.SWPALH(dest, dest, src); });
  }
};
struct ATOMIC_EXCHANGE_I32
    : Sequence<ATOMIC_EXCHANGE_I32,
               I<OPCODE_ATOMIC_EXCHANGE, I32Op, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAtomicExchangeXX<ATOMIC_EXCHANGE_I8, WReg>(
        e, i,
        [](A64Emitter& e, WReg dest, XReg src) { e.SWPAL(dest, dest, src); });
  }
};
struct ATOMIC_EXCHANGE_I64
    : Sequence<ATOMIC_EXCHANGE_I64,
               I<OPCODE_ATOMIC_EXCHANGE, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitAtomicExchangeXX<ATOMIC_EXCHANGE_I8, XReg>(
        e, i,
        [](A64Emitter& e, XReg dest, XReg src) { e.SWPAL(dest, dest, src); });
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
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.MOV(W0, i.src2);
    if (xe::memory::allocation_granularity() > 0x1000) {
      // Emulate the 4 KB physical address offset in 0xE0000000+ when can't do
      // it via memory mapping.
      e.MOV(W3, 0xE0000000);
      e.CMP(i.src1.reg(), X3);
      e.CSET(W1, Cond::HS);
      e.LSL(W1, W1, 12);
      e.ADD(W1, W1, i.src1.reg().toW());
    } else {
      e.MOV(W1, i.src1.reg().toW());
    }
    e.ADD(X1, e.GetMembaseReg(), X1);

    // if([C] == A) [C] = B
    // else A = [C]
    e.CASAL(W0, i.src3, X1);

    // Set dest to 1 in the case of a successful exchange
    e.CMP(W0, i.src2);
    e.CSET(i.dest, Cond::EQ);
  }
};
struct ATOMIC_COMPARE_EXCHANGE_I64
    : Sequence<ATOMIC_COMPARE_EXCHANGE_I64,
               I<OPCODE_ATOMIC_COMPARE_EXCHANGE, I8Op, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.MOV(X0, i.src2);
    if (xe::memory::allocation_granularity() > 0x1000) {
      // Emulate the 4 KB physical address offset in 0xE0000000+ when can't do
      // it via memory mapping.
      e.MOV(W3, 0xE0000000);
      e.CMP(i.src1.reg(), X3);
      e.CSET(W1, Cond::HS);
      e.LSL(W1, W1, 12);
      e.ADD(W1, W1, i.src1.reg().toW());
    } else {
      e.MOV(W1, i.src1.reg().toW());
    }
    e.ADD(X1, e.GetMembaseReg(), X1);

    // if([C] == A) [C] = B
    // else A = [C]
    e.CASAL(X0, i.src3, X1);

    // Set dest to 1 in the case of a successful exchange
    e.CMP(X0, i.src2);
    e.CSET(i.dest, Cond::EQ);
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
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.LDRB(i.dest, SP, i.src1.constant());
    // e.mov(i.dest, e.byte[e.rsp + i.src1.constant()]);
    // e.TraceLoadI8(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
struct LOAD_LOCAL_I16
    : Sequence<LOAD_LOCAL_I16, I<OPCODE_LOAD_LOCAL, I16Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.LDRH(i.dest, SP, i.src1.constant());
    // e.TraceLoadI16(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
struct LOAD_LOCAL_I32
    : Sequence<LOAD_LOCAL_I32, I<OPCODE_LOAD_LOCAL, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.LDR(i.dest, SP, i.src1.constant());
    // e.TraceLoadI32(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
struct LOAD_LOCAL_I64
    : Sequence<LOAD_LOCAL_I64, I<OPCODE_LOAD_LOCAL, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.LDR(i.dest, SP, i.src1.constant());
    // e.TraceLoadI64(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
struct LOAD_LOCAL_F32
    : Sequence<LOAD_LOCAL_F32, I<OPCODE_LOAD_LOCAL, F32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.LDR(i.dest, SP, i.src1.constant());
    // e.TraceLoadF32(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
struct LOAD_LOCAL_F64
    : Sequence<LOAD_LOCAL_F64, I<OPCODE_LOAD_LOCAL, F64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.LDR(i.dest, SP, i.src1.constant());
    // e.TraceLoadF64(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
struct LOAD_LOCAL_V128
    : Sequence<LOAD_LOCAL_V128, I<OPCODE_LOAD_LOCAL, V128Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.LDR(i.dest, SP, i.src1.constant());
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
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreI8(DATA_LOCAL, i.src1.constant, i.src2);
    e.STRB(i.src2, SP, i.src1.constant());
  }
};
struct STORE_LOCAL_I16
    : Sequence<STORE_LOCAL_I16, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreI16(DATA_LOCAL, i.src1.constant, i.src2);
    e.STRH(i.src2, SP, i.src1.constant());
  }
};
struct STORE_LOCAL_I32
    : Sequence<STORE_LOCAL_I32, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreI32(DATA_LOCAL, i.src1.constant, i.src2);
    e.STR(i.src2, SP, i.src1.constant());
  }
};
struct STORE_LOCAL_I64
    : Sequence<STORE_LOCAL_I64, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreI64(DATA_LOCAL, i.src1.constant, i.src2);
    e.STR(i.src2, SP, i.src1.constant());
  }
};
struct STORE_LOCAL_F32
    : Sequence<STORE_LOCAL_F32, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreF32(DATA_LOCAL, i.src1.constant, i.src2);
    e.STR(i.src2, SP, i.src1.constant());
  }
};
struct STORE_LOCAL_F64
    : Sequence<STORE_LOCAL_F64, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreF64(DATA_LOCAL, i.src1.constant, i.src2);
    e.STR(i.src2, SP, i.src1.constant());
  }
};
struct STORE_LOCAL_V128
    : Sequence<STORE_LOCAL_V128, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // e.TraceStoreV128(DATA_LOCAL, i.src1.constant, i.src2);
    e.STR(i.src2, SP, i.src1.constant());
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
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.LDRB(i.dest, e.GetContextReg(), i.src1.value);
    // e.mov(i.dest, e.byte[addr]);
    if (IsTracingData()) {
      // e.mov(e.GetNativeParam(0), i.src1.value);
      // e.mov(e.GetNativeParam(1), e.byte[addr]);
      // e.CallNative(reinterpret_cast<void*>(TraceContextLoadI8));
    }
  }
};
struct LOAD_CONTEXT_I16
    : Sequence<LOAD_CONTEXT_I16, I<OPCODE_LOAD_CONTEXT, I16Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.LDRH(i.dest, e.GetContextReg(), i.src1.value);
    if (IsTracingData()) {
      // e.mov(e.GetNativeParam(1), e.word[addr]);
      // e.mov(e.GetNativeParam(0), i.src1.value);
      // e.CallNative(reinterpret_cast<void*>(TraceContextLoadI16));
    }
  }
};
struct LOAD_CONTEXT_I32
    : Sequence<LOAD_CONTEXT_I32, I<OPCODE_LOAD_CONTEXT, I32Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.LDR(i.dest, e.GetContextReg(), i.src1.value);
    if (IsTracingData()) {
      // e.mov(e.GetNativeParam(1), e.dword[addr]);
      // e.mov(e.GetNativeParam(0), i.src1.value);
      // e.CallNative(reinterpret_cast<void*>(TraceContextLoadI32));
    }
  }
};
struct LOAD_CONTEXT_I64
    : Sequence<LOAD_CONTEXT_I64, I<OPCODE_LOAD_CONTEXT, I64Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.LDR(i.dest, e.GetContextReg(), i.src1.value);
    if (IsTracingData()) {
      // e.mov(e.GetNativeParam(1), e.qword[addr]);
      // e.mov(e.GetNativeParam(0), i.src1.value);
      // e.CallNative(reinterpret_cast<void*>(TraceContextLoadI64));
    }
  }
};
struct LOAD_CONTEXT_F32
    : Sequence<LOAD_CONTEXT_F32, I<OPCODE_LOAD_CONTEXT, F32Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.LDR(i.dest, e.GetContextReg(), i.src1.value);
    if (IsTracingData()) {
      // e.lea(e.GetNativeParam(1), e.dword[addr]);
      // e.mov(e.GetNativeParam(0), i.src1.value);
      // e.CallNative(reinterpret_cast<void*>(TraceContextLoadF32));
    }
  }
};
struct LOAD_CONTEXT_F64
    : Sequence<LOAD_CONTEXT_F64, I<OPCODE_LOAD_CONTEXT, F64Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.LDR(i.dest, e.GetContextReg(), i.src1.value);
    // e.vmovsd(i.dest, e.qword[addr]);
    if (IsTracingData()) {
      // e.lea(e.GetNativeParam(1), e.qword[addr]);
      // e.mov(e.GetNativeParam(0), i.src1.value);
      // e.CallNative(reinterpret_cast<void*>(TraceContextLoadF64));
    }
  }
};
struct LOAD_CONTEXT_V128
    : Sequence<LOAD_CONTEXT_V128, I<OPCODE_LOAD_CONTEXT, V128Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.LDR(i.dest, e.GetContextReg(), i.src1.value);
    if (IsTracingData()) {
      // e.lea(e.GetNativeParam(1), e.ptr[addr]);
      // e.mov(e.GetNativeParam(0), i.src1.value);
      // e.CallNative(reinterpret_cast<void*>(TraceContextLoadV128));
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
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.MOV(W0, i.src2.constant());
      e.STRB(W0, e.GetContextReg(), i.src1.value);
    } else {
      e.STRB(i.src2.reg(), e.GetContextReg(), i.src1.value);
    }
    if (IsTracingData()) {
      // e.mov(e.GetNativeParam(1), e.byte[addr]);
      // e.mov(e.GetNativeParam(0), i.src1.value);
      // e.CallNative(reinterpret_cast<void*>(TraceContextStoreI8));
    }
  }
};
struct STORE_CONTEXT_I16
    : Sequence<STORE_CONTEXT_I16,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.MOV(W0, i.src2.constant());
      e.STRH(W0, e.GetContextReg(), i.src1.value);
    } else {
      e.STRH(i.src2.reg(), e.GetContextReg(), i.src1.value);
    }
    if (IsTracingData()) {
      // e.mov(e.GetNativeParam(1), e.word[addr]);
      // e.mov(e.GetNativeParam(0), i.src1.value);
      // e.CallNative(reinterpret_cast<void*>(TraceContextStoreI16));
    }
  }
};
struct STORE_CONTEXT_I32
    : Sequence<STORE_CONTEXT_I32,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.MOV(W0, i.src2.constant());
      e.STR(W0, e.GetContextReg(), i.src1.value);
    } else {
      e.STR(i.src2.reg(), e.GetContextReg(), i.src1.value);
    }
    if (IsTracingData()) {
      // e.mov(e.GetNativeParam(1), e.dword[addr]);
      // e.mov(e.GetNativeParam(0), i.src1.value);
      // e.CallNative(reinterpret_cast<void*>(TraceContextStoreI32));
    }
  }
};
struct STORE_CONTEXT_I64
    : Sequence<STORE_CONTEXT_I64,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.MOV(X0, i.src2.constant());
      e.STR(X0, e.GetContextReg(), i.src1.value);
    } else {
      e.STR(i.src2.reg(), e.GetContextReg(), i.src1.value);
    }
    if (IsTracingData()) {
      // e.mov(e.GetNativeParam(1), e.qword[addr]);
      // e.mov(e.GetNativeParam(0), i.src1.value);
      // e.CallNative(reinterpret_cast<void*>(TraceContextStoreI64));
    }
  }
};
struct STORE_CONTEXT_F32
    : Sequence<STORE_CONTEXT_F32,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.MOV(W0, i.src2.value->constant.i32);
      e.STR(W0, e.GetContextReg(), i.src1.value);
    } else {
      e.STR(i.src2, e.GetContextReg(), i.src1.value);
    }
    if (IsTracingData()) {
      // e.lea(e.GetNativeParam(1), e.dword[addr]);
      // e.mov(e.GetNativeParam(0), i.src1.value);
      // e.CallNative(reinterpret_cast<void*>(TraceContextStoreF32));
    }
  }
};
struct STORE_CONTEXT_F64
    : Sequence<STORE_CONTEXT_F64,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.MOV(X0, i.src2.value->constant.i64);
      e.STR(X0, e.GetContextReg(), i.src1.value);
    } else {
      e.STR(i.src2, e.GetContextReg(), i.src1.value);
    }
    if (IsTracingData()) {
      // e.lea(e.GetNativeParam(1), e.qword[addr]);
      // e.mov(e.GetNativeParam(0), i.src1.value);
      // e.CallNative(reinterpret_cast<void*>(TraceContextStoreF64));
    }
  }
};
struct STORE_CONTEXT_V128
    : Sequence<STORE_CONTEXT_V128,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.LoadConstantV(Q0, i.src2.constant());
      e.STR(Q0, e.GetContextReg(), i.src1.value);
    } else {
      e.STR(i.src2, e.GetContextReg(), i.src1.value);
    }
    if (IsTracingData()) {
      // e.lea(e.GetNativeParam(1), e.ptr[addr]);
      // e.mov(e.GetNativeParam(0), i.src1.value);
      // e.CallNative(reinterpret_cast<void*>(TraceContextStoreV128));
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
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // uint64_t (context, addr)
    const auto mmio_range = reinterpret_cast<MMIORange*>(i.src1.value);
    const auto read_address = uint32_t(i.src2.value);
    e.MOV(e.GetNativeParam(0), uint64_t(mmio_range->callback_context));
    e.MOV(e.GetNativeParam(1).toW(), read_address);
    e.CallNativeSafe(reinterpret_cast<void*>(mmio_range->read));
    e.REV(i.dest, W0);
    if (IsTracingData()) {
      e.MOV(e.GetNativeParam(0).toW(), i.dest);
      e.MOV(X1, read_address);
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
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // void (context, addr, value)
    const auto mmio_range = reinterpret_cast<MMIORange*>(i.src1.value);
    const auto write_address = uint32_t(i.src2.value);
    e.MOV(e.GetNativeParam(0), uint64_t(mmio_range->callback_context));
    e.MOV(e.GetNativeParam(1).toW(), write_address);
    if (i.src3.is_constant) {
      e.MOV(e.GetNativeParam(2).toW(), xe::byte_swap(i.src3.constant()));
    } else {
      e.REV(e.GetNativeParam(2).toW(), i.src3);
    }
    e.CallNativeSafe(reinterpret_cast<void*>(mmio_range->write));
    if (IsTracingData()) {
      if (i.src3.is_constant) {
        e.MOV(e.GetNativeParam(0).toW(), i.src3.constant());
      } else {
        e.MOV(e.GetNativeParam(0).toW(), i.src3);
      }
      e.MOV(X1, write_address);
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
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddressOffset(e, i.src1, i.src2);
    e.LDRB(i.dest, addr_reg);
  }
};

struct LOAD_OFFSET_I16
    : Sequence<LOAD_OFFSET_I16, I<OPCODE_LOAD_OFFSET, I16Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddressOffset(e, i.src1, i.src2);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      e.LDRH(i.dest, addr_reg);
      e.REV16(i.dest, i.dest);
    } else {
      e.LDRH(i.dest, addr_reg);
    }
  }
};

struct LOAD_OFFSET_I32
    : Sequence<LOAD_OFFSET_I32, I<OPCODE_LOAD_OFFSET, I32Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddressOffset(e, i.src1, i.src2);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      e.LDR(i.dest, addr_reg);
      e.REV(i.dest, i.dest);
    } else {
      e.LDR(i.dest, addr_reg);
    }
  }
};

struct LOAD_OFFSET_I64
    : Sequence<LOAD_OFFSET_I64, I<OPCODE_LOAD_OFFSET, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddressOffset(e, i.src1, i.src2);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      e.LDR(i.dest, addr_reg);
      e.REV(i.dest, i.dest);
    } else {
      e.LDR(i.dest, addr_reg);
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
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddressOffset(e, i.src1, i.src2);
    if (i.src3.is_constant) {
      e.MOV(W0, i.src3.constant());
      e.STRB(W0, addr_reg);
    } else {
      e.STRB(i.src3, addr_reg);
    }
  }
};

struct STORE_OFFSET_I16
    : Sequence<STORE_OFFSET_I16,
               I<OPCODE_STORE_OFFSET, VoidOp, I64Op, I64Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddressOffset(e, i.src1, i.src2);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_false(i.src3.is_constant);
      assert_always("not implemented");
    } else {
      if (i.src3.is_constant) {
        e.MOV(W0, i.src3.constant());
        e.STRH(W0, addr_reg);
      } else {
        e.STRH(i.src3, addr_reg);
      }
    }
  }
};

struct STORE_OFFSET_I32
    : Sequence<STORE_OFFSET_I32,
               I<OPCODE_STORE_OFFSET, VoidOp, I64Op, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddressOffset(e, i.src1, i.src2);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_false(i.src3.is_constant);
      assert_always("not implemented");
    } else {
      if (i.src3.is_constant) {
        e.MOV(W0, i.src3.constant());
        e.STR(W0, addr_reg);
      } else {
        e.STR(i.src3, addr_reg);
      }
    }
  }
};

struct STORE_OFFSET_I64
    : Sequence<STORE_OFFSET_I64,
               I<OPCODE_STORE_OFFSET, VoidOp, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddressOffset(e, i.src1, i.src2);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_false(i.src3.is_constant);
      assert_always("not implemented");
    } else {
      if (i.src3.is_constant) {
        e.MovMem64(addr_reg, 0, i.src3.constant());
      } else {
        e.STR(i.src3, addr_reg);
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
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddress(e, i.src1);
    e.LDRB(i.dest, addr_reg);
    if (IsTracingData()) {
      // e.mov(e.GetNativeParam(1).cvt8(), i.dest);
      // e.lea(e.GetNativeParam(0), e.ptr[addr]);
      // e.CallNative(reinterpret_cast<void*>(TraceMemoryLoadI8));
    }
  }
};
struct LOAD_I16 : Sequence<LOAD_I16, I<OPCODE_LOAD, I16Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      e.LDRH(i.dest, addr_reg);
      e.REV16(i.dest, i.dest);
    } else {
      e.LDRH(i.dest, addr_reg);
    }
    if (IsTracingData()) {
      // e.mov(e.GetNativeParam(1).cvt16(), i.dest);
      // e.lea(e.GetNativeParam(0), e.ptr[addr]);
      // e.CallNative(reinterpret_cast<void*>(TraceMemoryLoadI16));
    }
  }
};
struct LOAD_I32 : Sequence<LOAD_I32, I<OPCODE_LOAD, I32Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      e.LDR(i.dest, addr_reg);
      e.REV(i.dest, i.dest);
    } else {
      e.LDR(i.dest, addr_reg);
    }
    if (IsTracingData()) {
      // e.mov(e.GetNativeParam(1).cvt32(), i.dest);
      // e.lea(e.GetNativeParam(0), e.ptr[addr]);
      // e.CallNative(reinterpret_cast<void*>(TraceMemoryLoadI32));
    }
  }
};
struct LOAD_I64 : Sequence<LOAD_I64, I<OPCODE_LOAD, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      e.LDR(i.dest, addr_reg);
      e.REV64(i.dest, i.dest);
    } else {
      e.LDR(i.dest, addr_reg);
    }
    if (IsTracingData()) {
      // e.mov(e.GetNativeParam(1), i.dest);
      // e.lea(e.GetNativeParam(0), e.ptr[addr]);
      // e.CallNative(reinterpret_cast<void*>(TraceMemoryLoadI64));
    }
  }
};
struct LOAD_F32 : Sequence<LOAD_F32, I<OPCODE_LOAD, F32Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddress(e, i.src1);
    e.LDR(i.dest, addr_reg);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_always("not implemented yet");
    }
    if (IsTracingData()) {
      // e.lea(e.GetNativeParam(1), e.dword[addr]);
      // e.lea(e.GetNativeParam(0), e.ptr[addr]);
      // e.CallNative(reinterpret_cast<void*>(TraceMemoryLoadF32));
    }
  }
};
struct LOAD_F64 : Sequence<LOAD_F64, I<OPCODE_LOAD, F64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddress(e, i.src1);
    e.LDR(i.dest, addr_reg);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_always("not implemented yet");
    }
    if (IsTracingData()) {
      // e.lea(e.GetNativeParam(1), e.qword[addr]);
      // e.lea(e.GetNativeParam(0), e.ptr[addr]);
      // e.CallNative(reinterpret_cast<void*>(TraceMemoryLoadF64));
    }
  }
};
struct LOAD_V128 : Sequence<LOAD_V128, I<OPCODE_LOAD, V128Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddress(e, i.src1);
    e.LDR(i.dest, addr_reg);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      // Reverse upper and lower 64-bit halfs
      e.REV64(i.dest.reg().B16(), i.dest.reg().B16());
      // Reverse the 64-bit halfs themselves
      e.EXT(i.dest.reg().B16(), i.dest.reg().B16(), i.dest.reg().B16(), 8);
    }
    if (IsTracingData()) {
      // e.lea(e.GetNativeParam(1), e.ptr[addr]);
      // e.lea(e.GetNativeParam(0), e.ptr[addr]);
      // e.CallNative(reinterpret_cast<void*>(TraceMemoryLoadV128));
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
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.MOV(W0, i.src2.constant());
      e.STRB(W0, addr_reg);
    } else {
      e.STRB(i.src2.reg(), addr_reg);
    }
    if (IsTracingData()) {
      addr_reg = ComputeMemoryAddress(e, i.src1);
      // e.mov(e.GetNativeParam(1).cvt8(), e.byte[addr]);
      // e.lea(e.GetNativeParam(0), e.ptr[addr]);
      // e.CallNative(reinterpret_cast<void*>(TraceMemoryStoreI8));
    }
  }
};
struct STORE_I16 : Sequence<STORE_I16, I<OPCODE_STORE, VoidOp, I64Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_false(i.src2.is_constant);
      assert_always("not implemented");
    } else {
      if (i.src2.is_constant) {
        e.MOV(W0, i.src2.constant());
        e.STRH(W0, addr_reg);
      } else {
        e.STRH(i.src2.reg(), addr_reg);
      }
    }
    if (IsTracingData()) {
      addr_reg = ComputeMemoryAddress(e, i.src1);
      // e.mov(e.GetNativeParam(1).cvt16(), e.word[addr]);
      // e.lea(e.GetNativeParam(0), e.ptr[addr]);
      // e.CallNative(reinterpret_cast<void*>(TraceMemoryStoreI16));
    }
  }
};
struct STORE_I32 : Sequence<STORE_I32, I<OPCODE_STORE, VoidOp, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_false(i.src2.is_constant);
      assert_always("not implemented");
    } else {
      if (i.src2.is_constant) {
        e.MOV(W0, i.src2.constant());
        e.STR(W0, addr_reg);
      } else {
        e.STR(i.src2.reg(), addr_reg);
      }
    }
    if (IsTracingData()) {
      addr_reg = ComputeMemoryAddress(e, i.src1);
      // e.mov(e.GetNativeParam(1).cvt32(), e.dword[addr]);
      // e.lea(e.GetNativeParam(0), e.ptr[addr]);
      // e.CallNative(reinterpret_cast<void*>(TraceMemoryStoreI32));
    }
  }
};
struct STORE_I64 : Sequence<STORE_I64, I<OPCODE_STORE, VoidOp, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_false(i.src2.is_constant);
      assert_always("not implemented");
    } else {
      if (i.src2.is_constant) {
        e.MovMem64(addr_reg, 0, i.src2.constant());
      } else {
        e.STR(i.src2.reg(), addr_reg);
      }
    }
    if (IsTracingData()) {
      addr_reg = ComputeMemoryAddress(e, i.src1);
      // e.mov(e.GetNativeParam(1), e.qword[addr]);
      // e.lea(e.GetNativeParam(0), e.ptr[addr]);
      // e.CallNative(reinterpret_cast<void*>(TraceMemoryStoreI64));
    }
  }
};
struct STORE_F32 : Sequence<STORE_F32, I<OPCODE_STORE, VoidOp, I64Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_false(i.src2.is_constant);
      assert_always("not yet implemented");
    } else {
      if (i.src2.is_constant) {
        e.MOV(W0, i.src2.value->constant.i32);
        e.STR(W0, addr_reg);
      } else {
        e.STR(i.src2, addr_reg);
      }
    }
    if (IsTracingData()) {
      addr_reg = ComputeMemoryAddress(e, i.src1);
      // e.lea(e.GetNativeParam(1), e.ptr[addr]);
      // e.lea(e.GetNativeParam(0), e.ptr[addr]);
      // e.CallNative(reinterpret_cast<void*>(TraceMemoryStoreF32));
    }
  }
};
struct STORE_F64 : Sequence<STORE_F64, I<OPCODE_STORE, VoidOp, I64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_false(i.src2.is_constant);
      assert_always("not yet implemented");
    } else {
      if (i.src2.is_constant) {
        e.MOV(X0, i.src2.value->constant.i64);
        e.STR(X0, addr_reg);
      } else {
        e.STR(i.src2, addr_reg);
      }
    }
    if (IsTracingData()) {
      addr_reg = ComputeMemoryAddress(e, i.src1);
      // e.lea(e.GetNativeParam(1), e.ptr[addr]);
      // e.lea(e.GetNativeParam(0), e.ptr[addr]);
      // e.CallNative(reinterpret_cast<void*>(TraceMemoryStoreF64));
    }
  }
};
struct STORE_V128
    : Sequence<STORE_V128, I<OPCODE_STORE, VoidOp, I64Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto addr_reg = ComputeMemoryAddress(e, i.src1);
    if (i.instr->flags & LoadStoreFlags::LOAD_STORE_BYTE_SWAP) {
      assert_false(i.src2.is_constant);
      // Reverse upper and lower 64-bit halfs
      e.REV64(Q0.B16(), i.src2.reg().B16());
      // Reverse the 64-bit halfs themselves
      e.EXT(Q0.B16(), Q0.B16(), Q0.B16(), 8);
      e.STR(Q0, addr_reg);
    } else {
      if (i.src2.is_constant) {
        e.LoadConstantV(Q0, i.src2.constant());
        e.STR(Q0, addr_reg);
      } else {
        e.STR(i.src2, addr_reg);
      }
    }
    if (IsTracingData()) {
      addr_reg = ComputeMemoryAddress(e, i.src1);
      // e.lea(e.GetNativeParam(1), e.ptr[addr]);
      // e.lea(e.GetNativeParam(0), e.ptr[addr]);
      // e.CallNative(reinterpret_cast<void*>(TraceMemoryStoreV128));
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
  static void Emit(A64Emitter& e, const EmitArgType& i) {
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

    // RegExp addr;
    // uint32_t address_constant;
    // if (i.src1.is_constant) {
    //   // TODO(benvanik): figure out how to do this without a temp.
    //   // Since the constant is often 0x8... if we tried to use that as a
    //   // displacement it would be sign extended and mess things up.
    //   address_constant = static_cast<uint32_t>(i.src1.constant());
    //   if (address_constant < 0x80000000) {
    //     addr = e.GetMembaseReg() + address_constant;
    //   } else {
    //     if (address_constant >= 0xE0000000 &&
    //         xe::memory::allocation_granularity() > 0x1000) {
    //       // e.mov(e.eax, address_constant + 0x1000);
    //     } else {
    //       // e.mov(e.eax, address_constant);
    //     }
    //     addr = e.GetMembaseReg() + e.rax;
    //   }
    // } else {
    //   if (xe::memory::allocation_granularity() > 0x1000) {
    //     // Emulate the 4 KB physical address offset in 0xE0000000+ when can't
    //     do
    //     // it via memory mapping.
    //     // e.cmp(i.src1.reg().cvt32(), 0xE0000000);
    //     // e.setae(e.al);
    //     // e.movzx(e.eax, e.al);
    //     // e.shl(e.eax, 12);
    //     // e.add(e.eax, i.src1.reg().cvt32());
    //   } else {
    //     // Clear the top 32 bits, as they are likely garbage.
    //     // TODO(benvanik): find a way to avoid doing this.
    //     // e.mov(e.eax, i.src1.reg().cvt32());
    //   }
    //   addr = e.GetMembaseReg() + e.rax;
    // }
    // if (is_clflush) {
    //   // e.clflush(e.ptr[addr]);
    // }
    // if (is_prefetch) {
    //   // e.prefetcht0(e.ptr[addr]);
    // }

    // if (cache_line_size >= 128) {
    //   // Prefetch the other 64 bytes of the 128-byte cache line.
    //   if (i.src1.is_constant && address_constant < 0x80000000) {
    //     addr = e.GetMembaseReg() + (address_constant ^ 64);
    //   } else {
    //     // e.xor_(e.eax, 64);
    //   }
    //   if (is_clflush) {
    //     // e.clflush(e.ptr[addr]);
    //   }
    //   if (is_prefetch) {
    //     // e.prefetcht0(e.ptr[addr]);
    //   }
    //   assert_true(cache_line_size == 128);
    // }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CACHE_CONTROL, CACHE_CONTROL);

// ============================================================================
// OPCODE_MEMORY_BARRIER
// ============================================================================
struct MEMORY_BARRIER
    : Sequence<MEMORY_BARRIER, I<OPCODE_MEMORY_BARRIER, VoidOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // mfence on x64 flushes all writes before any later instructions
    // e.mfence();

    // This is equivalent to DMB SY
    e.DMB(BarrierOp::SY);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MEMORY_BARRIER, MEMORY_BARRIER);

// ============================================================================
// OPCODE_MEMSET
// ============================================================================
struct MEMSET_I64_I8_I64
    : Sequence<MEMSET_I64_I8_I64,
               I<OPCODE_MEMSET, VoidOp, I64Op, I8Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    assert_true(i.src3.is_constant);
    assert_true(i.src2.constant() == 0);
    e.EOR(Q0.B16(), Q0.B16(), Q0.B16());
    auto addr_reg = ComputeMemoryAddress(e, i.src1);
    switch (i.src3.constant()) {
      case 32:
        e.STR(Q0, addr_reg, 0 * 16);
        e.STR(Q0, addr_reg, 1 * 16);
        break;
      case 128:
        e.STR(Q0, addr_reg, 0 * 16);
        e.STR(Q0, addr_reg, 1 * 16);
        e.STR(Q0, addr_reg, 2 * 16);
        e.STR(Q0, addr_reg, 3 * 16);
        e.STR(Q0, addr_reg, 4 * 16);
        e.STR(Q0, addr_reg, 5 * 16);
        e.STR(Q0, addr_reg, 6 * 16);
        e.STR(Q0, addr_reg, 7 * 16);
        break;
      default:
        assert_unhandled_case(i.src3.constant());
        break;
    }
    if (IsTracingData()) {
      addr_reg = ComputeMemoryAddress(e, i.src1);
      e.MOV(e.GetNativeParam(2), i.src3.constant());
      e.MOV(e.GetNativeParam(1), i.src2.constant());
      e.LDR(e.GetNativeParam(0), addr_reg);
      e.CallNative(reinterpret_cast<void*>(TraceMemset));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MEMSET, MEMSET_I64_I8_I64);

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
