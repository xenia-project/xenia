/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// A note about vectors:
// Alloy represents vectors as xyzw pairs, with indices 0123.
// XMM registers are xyzw pairs with indices 3210, making them more like wzyx.
// This makes things somewhat confusing. It'd be nice to just shuffle the
// registers around on load/store, however certain operations require that
// data be in the right offset.
// Basically, this identity must hold:
//   shuffle(vec, b00011011) -> {x,y,z,w} => {x,y,z,w}
// All indices and operations must respect that.
//
// Memory (big endian):
// [00 01 02 03] [04 05 06 07] [08 09 0A 0B] [0C 0D 0E 0F] (x, y, z, w)
// load into xmm register:
// [0F 0E 0D 0C] [0B 0A 09 08] [07 06 05 04] [03 02 01 00] (w, z, y, x)

#include <alloy/backend/x64/x64_sequences.h>

#include <alloy/backend/x64/x64_emitter.h>
#include <alloy/backend/x64/x64_tracers.h>
#include <alloy/hir/hir_builder.h>
#include <alloy/runtime/runtime.h>

// TODO(benvanik): reimplement packing functions
#include <DirectXPackedVector.h>

using namespace alloy;
using namespace alloy::backend;
using namespace alloy::backend::x64;
using namespace alloy::hir;
using namespace alloy::runtime;

using namespace Xbyak;

// Utilities/types used only in this file:
#include <alloy/backend/x64/x64_sequence.inl>

namespace {
static std::unordered_multimap<uint32_t, SequenceSelectFn> sequence_table;
}  // namespace


// Selects the right byte/word/etc from a vector. We need to flip logical
// indices (0,1,2,3,4,5,6,7,...) = (3,2,1,0,7,6,5,4,...)
#define VEC128_B(n) ((n) & 0xC) | ((~(n)) & 0x3)
#define VEC128_W(n) ((n) & 0x6) | ((~(n)) & 0x1)
#define VEC128_D(n) (n)
#define VEC128_F(n) (n)


// ============================================================================
// OPCODE_COMMENT
// ============================================================================
EMITTER(COMMENT, MATCH(I<OPCODE_COMMENT, VoidOp, OffsetOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (IsTracingInstr()) {
      auto str = reinterpret_cast<const char*>(i.src1.value);
      // TODO(benvanik): pass through.
      // TODO(benvanik): don't just leak this memory.
      auto str_copy = xestrdupa(str);
      e.mov(e.rdx, reinterpret_cast<uint64_t>(str_copy));
      e.CallNative(TraceString);
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_COMMENT,
    COMMENT);


// ============================================================================
// OPCODE_NOP
// ============================================================================
EMITTER(NOP, MATCH(I<OPCODE_NOP, VoidOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.nop();
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_NOP,
    NOP);


// ============================================================================
// OPCODE_SOURCE_OFFSET
// ============================================================================
EMITTER(SOURCE_OFFSET, MATCH(I<OPCODE_SOURCE_OFFSET, VoidOp, OffsetOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
#if XE_DEBUG
    e.nop();
    e.nop();
    e.mov(e.eax, (uint32_t)i.src1.value);
    e.nop();
    e.nop();
#endif  // XE_DEBUG
    e.MarkSourceOffset(i.instr);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_SOURCE_OFFSET,
    SOURCE_OFFSET);


// ============================================================================
// OPCODE_DEBUG_BREAK
// ============================================================================
EMITTER(DEBUG_BREAK, MATCH(I<OPCODE_DEBUG_BREAK, VoidOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.DebugBreak();
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_DEBUG_BREAK,
    DEBUG_BREAK);


// ============================================================================
// OPCODE_DEBUG_BREAK_TRUE
// ============================================================================
EMITTER(DEBUG_BREAK_TRUE_I8, MATCH(I<OPCODE_DEBUG_BREAK_TRUE, VoidOp, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.DebugBreak();
    e.L(skip);
  }
};
EMITTER(DEBUG_BREAK_TRUE_I16, MATCH(I<OPCODE_DEBUG_BREAK_TRUE, VoidOp, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.DebugBreak();
    e.L(skip);
  }
};
EMITTER(DEBUG_BREAK_TRUE_I32, MATCH(I<OPCODE_DEBUG_BREAK_TRUE, VoidOp, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.DebugBreak();
    e.L(skip);
  }
};
EMITTER(DEBUG_BREAK_TRUE_I64, MATCH(I<OPCODE_DEBUG_BREAK_TRUE, VoidOp, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.DebugBreak();
    e.L(skip);
  }
};
EMITTER(DEBUG_BREAK_TRUE_F32, MATCH(I<OPCODE_DEBUG_BREAK_TRUE, VoidOp, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.DebugBreak();
    e.L(skip);
  }
};
EMITTER(DEBUG_BREAK_TRUE_F64, MATCH(I<OPCODE_DEBUG_BREAK_TRUE, VoidOp, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.DebugBreak();
    e.L(skip);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_DEBUG_BREAK_TRUE,
    DEBUG_BREAK_TRUE_I8,
    DEBUG_BREAK_TRUE_I16,
    DEBUG_BREAK_TRUE_I32,
    DEBUG_BREAK_TRUE_I64,
    DEBUG_BREAK_TRUE_F32,
    DEBUG_BREAK_TRUE_F64);


// ============================================================================
// OPCODE_TRAP
// ============================================================================
EMITTER(TRAP, MATCH(I<OPCODE_TRAP, VoidOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.Trap();
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_TRAP,
    TRAP);


// ============================================================================
// OPCODE_TRAP_TRUE
// ============================================================================
EMITTER(TRAP_TRUE_I8, MATCH(I<OPCODE_TRAP_TRUE, VoidOp, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Trap();
    e.L(skip);
  }
};
EMITTER(TRAP_TRUE_I16, MATCH(I<OPCODE_TRAP_TRUE, VoidOp, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Trap();
    e.L(skip);
  }
};
EMITTER(TRAP_TRUE_I32, MATCH(I<OPCODE_TRAP_TRUE, VoidOp, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Trap();
    e.L(skip);
  }
};
EMITTER(TRAP_TRUE_I64, MATCH(I<OPCODE_TRAP_TRUE, VoidOp, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Trap();
    e.L(skip);
  }
};
EMITTER(TRAP_TRUE_F32, MATCH(I<OPCODE_TRAP_TRUE, VoidOp, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Trap();
    e.L(skip);
  }
};
EMITTER(TRAP_TRUE_F64, MATCH(I<OPCODE_TRAP_TRUE, VoidOp, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Trap();
    e.L(skip);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_TRAP_TRUE,
    TRAP_TRUE_I8,
    TRAP_TRUE_I16,
    TRAP_TRUE_I32,
    TRAP_TRUE_I64,
    TRAP_TRUE_F32,
    TRAP_TRUE_F64);


// ============================================================================
// OPCODE_CALL
// ============================================================================
EMITTER(CALL, MATCH(I<OPCODE_CALL, VoidOp, SymbolOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.Call(i.instr, i.src1.value);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_CALL,
    CALL);


// ============================================================================
// OPCODE_CALL_TRUE
// ============================================================================
EMITTER(CALL_TRUE_I8, MATCH(I<OPCODE_CALL_TRUE, VoidOp, I8<>, SymbolOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Call(i.instr, i.src2.value);
    e.L(skip);
  }
};
EMITTER(CALL_TRUE_I16, MATCH(I<OPCODE_CALL_TRUE, VoidOp, I16<>, SymbolOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Call(i.instr, i.src2.value);
    e.L(skip);
  }
};
EMITTER(CALL_TRUE_I32, MATCH(I<OPCODE_CALL_TRUE, VoidOp, I32<>, SymbolOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Call(i.instr, i.src2.value);
    e.L(skip);
  }
};
EMITTER(CALL_TRUE_I64, MATCH(I<OPCODE_CALL_TRUE, VoidOp, I64<>, SymbolOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Call(i.instr, i.src2.value);
    e.L(skip);
  }
};
EMITTER(CALL_TRUE_F32, MATCH(I<OPCODE_CALL_TRUE, VoidOp, F32<>, SymbolOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Call(i.instr, i.src2.value);
    e.L(skip);
  }
};
EMITTER(CALL_TRUE_F64, MATCH(I<OPCODE_CALL_TRUE, VoidOp, F64<>, SymbolOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Call(i.instr, i.src2.value);
    e.L(skip);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_CALL_TRUE,
    CALL_TRUE_I8,
    CALL_TRUE_I16,
    CALL_TRUE_I32,
    CALL_TRUE_I64,
    CALL_TRUE_F32,
    CALL_TRUE_F64);


// ============================================================================
// OPCODE_CALL_INDIRECT
// ============================================================================
EMITTER(CALL_INDIRECT, MATCH(I<OPCODE_CALL_INDIRECT, VoidOp, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.CallIndirect(i.instr, i.src1);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_CALL_INDIRECT,
    CALL_INDIRECT);


// ============================================================================
// OPCODE_CALL_INDIRECT_TRUE
// ============================================================================
EMITTER(CALL_INDIRECT_TRUE_I8, MATCH(I<OPCODE_CALL_INDIRECT_TRUE, VoidOp, I8<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.CallIndirect(i.instr, i.src2);
    e.L(skip);
  }
};
EMITTER(CALL_INDIRECT_TRUE_I16, MATCH(I<OPCODE_CALL_INDIRECT_TRUE, VoidOp, I16<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.CallIndirect(i.instr, i.src2);
    e.L(skip);
  }
};
EMITTER(CALL_INDIRECT_TRUE_I32, MATCH(I<OPCODE_CALL_INDIRECT_TRUE, VoidOp, I32<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.CallIndirect(i.instr, i.src2);
    e.L(skip);
  }
};
EMITTER(CALL_INDIRECT_TRUE_I64, MATCH(I<OPCODE_CALL_INDIRECT_TRUE, VoidOp, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.CallIndirect(i.instr, i.src2);
    e.L(skip);
  }
};
EMITTER(CALL_INDIRECT_TRUE_F32, MATCH(I<OPCODE_CALL_INDIRECT_TRUE, VoidOp, F32<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.CallIndirect(i.instr, i.src2);
    e.L(skip);
  }
};
EMITTER(CALL_INDIRECT_TRUE_F64, MATCH(I<OPCODE_CALL_INDIRECT_TRUE, VoidOp, F64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.CallIndirect(i.instr, i.src2);
    e.L(skip);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_CALL_INDIRECT_TRUE,
    CALL_INDIRECT_TRUE_I8,
    CALL_INDIRECT_TRUE_I16,
    CALL_INDIRECT_TRUE_I32,
    CALL_INDIRECT_TRUE_I64,
    CALL_INDIRECT_TRUE_F32,
    CALL_INDIRECT_TRUE_F64);


// ============================================================================
// OPCODE_CALL_EXTERN
// ============================================================================
EMITTER(CALL_EXTERN, MATCH(I<OPCODE_CALL_EXTERN, VoidOp, SymbolOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.CallExtern(i.instr, i.src1.value);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_CALL_EXTERN,
    CALL_EXTERN);


// ============================================================================
// OPCODE_RETURN
// ============================================================================
EMITTER(RETURN, MATCH(I<OPCODE_RETURN, VoidOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // If this is the last instruction in the last block, just let us
    // fall through.
    if (i.instr->next || i.instr->block->next) {
      e.jmp("epilog", CodeGenerator::T_NEAR);
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_RETURN,
    RETURN);


// ============================================================================
// OPCODE_RETURN_TRUE
// ============================================================================
EMITTER(RETURN_TRUE_I8, MATCH(I<OPCODE_RETURN_TRUE, VoidOp, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jnz("epilog", CodeGenerator::T_NEAR);
  }
};
EMITTER(RETURN_TRUE_I16, MATCH(I<OPCODE_RETURN_TRUE, VoidOp, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jnz("epilog", CodeGenerator::T_NEAR);
  }
};
EMITTER(RETURN_TRUE_I32, MATCH(I<OPCODE_RETURN_TRUE, VoidOp, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jnz("epilog", CodeGenerator::T_NEAR);
  }
};
EMITTER(RETURN_TRUE_I64, MATCH(I<OPCODE_RETURN_TRUE, VoidOp, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jnz("epilog", CodeGenerator::T_NEAR);
  }
};
EMITTER(RETURN_TRUE_F32, MATCH(I<OPCODE_RETURN_TRUE, VoidOp, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.jnz("epilog", CodeGenerator::T_NEAR);
  }
};
EMITTER(RETURN_TRUE_F64, MATCH(I<OPCODE_RETURN_TRUE, VoidOp, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.jnz("epilog", CodeGenerator::T_NEAR);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_RETURN_TRUE,
    RETURN_TRUE_I8,
    RETURN_TRUE_I16,
    RETURN_TRUE_I32,
    RETURN_TRUE_I64,
    RETURN_TRUE_F32,
    RETURN_TRUE_F64);


// ============================================================================
// OPCODE_SET_RETURN_ADDRESS
// ============================================================================
EMITTER(SET_RETURN_ADDRESS, MATCH(I<OPCODE_SET_RETURN_ADDRESS, VoidOp, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.SetReturnAddress(i.src1.constant());
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_SET_RETURN_ADDRESS,
    SET_RETURN_ADDRESS);


// ============================================================================
// OPCODE_BRANCH
// ============================================================================
EMITTER(BRANCH, MATCH(I<OPCODE_BRANCH, VoidOp, LabelOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.jmp(i.src1.value->name, e.T_NEAR);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_BRANCH,
    BRANCH);


// ============================================================================
// OPCODE_BRANCH_TRUE
// ============================================================================
EMITTER(BRANCH_TRUE_I8, MATCH(I<OPCODE_BRANCH_TRUE, VoidOp, I8<>, LabelOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jnz(i.src2.value->name, e.T_NEAR);
  }
};
EMITTER(BRANCH_TRUE_I16, MATCH(I<OPCODE_BRANCH_TRUE, VoidOp, I16<>, LabelOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jnz(i.src2.value->name, e.T_NEAR);
  }
};
EMITTER(BRANCH_TRUE_I32, MATCH(I<OPCODE_BRANCH_TRUE, VoidOp, I32<>, LabelOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jnz(i.src2.value->name, e.T_NEAR);
  }
};
EMITTER(BRANCH_TRUE_I64, MATCH(I<OPCODE_BRANCH_TRUE, VoidOp, I64<>, LabelOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jnz(i.src2.value->name, e.T_NEAR);
  }
};
EMITTER(BRANCH_TRUE_F32, MATCH(I<OPCODE_BRANCH_TRUE, VoidOp, F32<>, LabelOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.jnz(i.src2.value->name, e.T_NEAR);
  }
};
EMITTER(BRANCH_TRUE_F64, MATCH(I<OPCODE_BRANCH_TRUE, VoidOp, F64<>, LabelOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.jnz(i.src2.value->name, e.T_NEAR);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_BRANCH_TRUE,
    BRANCH_TRUE_I8,
    BRANCH_TRUE_I16,
    BRANCH_TRUE_I32,
    BRANCH_TRUE_I64,
    BRANCH_TRUE_F32,
    BRANCH_TRUE_F64);


// ============================================================================
// OPCODE_BRANCH_FALSE
// ============================================================================
EMITTER(BRANCH_FALSE_I8, MATCH(I<OPCODE_BRANCH_FALSE, VoidOp, I8<>, LabelOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jz(i.src2.value->name, e.T_NEAR);
  }
};
EMITTER(BRANCH_FALSE_I16, MATCH(I<OPCODE_BRANCH_FALSE, VoidOp, I16<>, LabelOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jz(i.src2.value->name, e.T_NEAR);
  }
};
EMITTER(BRANCH_FALSE_I32, MATCH(I<OPCODE_BRANCH_FALSE, VoidOp, I32<>, LabelOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jz(i.src2.value->name, e.T_NEAR);
  }
};
EMITTER(BRANCH_FALSE_I64, MATCH(I<OPCODE_BRANCH_FALSE, VoidOp, I64<>, LabelOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jz(i.src2.value->name, e.T_NEAR);
  }
};
EMITTER(BRANCH_FALSE_F32, MATCH(I<OPCODE_BRANCH_FALSE, VoidOp, F32<>, LabelOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.jz(i.src2.value->name, e.T_NEAR);
  }
};
EMITTER(BRANCH_FALSE_F64, MATCH(I<OPCODE_BRANCH_FALSE, VoidOp, F64<>, LabelOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.jz(i.src2.value->name, e.T_NEAR);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_BRANCH_FALSE,
    BRANCH_FALSE_I8,
    BRANCH_FALSE_I16,
    BRANCH_FALSE_I32,
    BRANCH_FALSE_I64,
    BRANCH_FALSE_F32,
    BRANCH_FALSE_F64);


// ============================================================================
// OPCODE_ASSIGN
// ============================================================================
EMITTER(ASSIGN_I8, MATCH(I<OPCODE_ASSIGN, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, i.src1);
  }
};
EMITTER(ASSIGN_I16, MATCH(I<OPCODE_ASSIGN, I16<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, i.src1);
  }
};
EMITTER(ASSIGN_I32, MATCH(I<OPCODE_ASSIGN, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, i.src1);
  }
};
EMITTER(ASSIGN_I64, MATCH(I<OPCODE_ASSIGN, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, i.src1);
  }
};
EMITTER(ASSIGN_F32, MATCH(I<OPCODE_ASSIGN, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovaps(i.dest, i.src1);
  }
};
EMITTER(ASSIGN_F64, MATCH(I<OPCODE_ASSIGN, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovaps(i.dest, i.src1);
  }
};
EMITTER(ASSIGN_V128, MATCH(I<OPCODE_ASSIGN, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovaps(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_ASSIGN,
    ASSIGN_I8,
    ASSIGN_I16,
    ASSIGN_I32,
    ASSIGN_I64,
    ASSIGN_F32,
    ASSIGN_F64,
    ASSIGN_V128);


// ============================================================================
// OPCODE_CAST
// ============================================================================
EMITTER(CAST_I32_F32, MATCH(I<OPCODE_CAST, I32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovd(i.dest, i.src1);
  }
};
EMITTER(CAST_I64_F64, MATCH(I<OPCODE_CAST, I64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovq(i.dest, i.src1);
  }
};
EMITTER(CAST_F32_I32, MATCH(I<OPCODE_CAST, F32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovd(i.dest, i.src1);
  }
};
EMITTER(CAST_F64_I64, MATCH(I<OPCODE_CAST, F64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovq(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_CAST,
    CAST_I32_F32,
    CAST_I64_F64,
    CAST_F32_I32,
    CAST_F64_I64);


// ============================================================================
// OPCODE_ZERO_EXTEND
// ============================================================================
EMITTER(ZERO_EXTEND_I16_I8, MATCH(I<OPCODE_ZERO_EXTEND, I16<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest, i.src1);
  }
};
EMITTER(ZERO_EXTEND_I32_I8, MATCH(I<OPCODE_ZERO_EXTEND, I32<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest, i.src1);
  }
};
EMITTER(ZERO_EXTEND_I64_I8, MATCH(I<OPCODE_ZERO_EXTEND, I64<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest, i.src1);
  }
};
EMITTER(ZERO_EXTEND_I32_I16, MATCH(I<OPCODE_ZERO_EXTEND, I32<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest, i.src1);
  }
};
EMITTER(ZERO_EXTEND_I64_I16, MATCH(I<OPCODE_ZERO_EXTEND, I64<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest, i.src1);
  }
};
EMITTER(ZERO_EXTEND_I64_I32, MATCH(I<OPCODE_ZERO_EXTEND, I64<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest.reg().cvt32(), i.src1);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_ZERO_EXTEND,
    ZERO_EXTEND_I16_I8,
    ZERO_EXTEND_I32_I8,
    ZERO_EXTEND_I64_I8,
    ZERO_EXTEND_I32_I16,
    ZERO_EXTEND_I64_I16,
    ZERO_EXTEND_I64_I32);


// ============================================================================
// OPCODE_SIGN_EXTEND
// ============================================================================
EMITTER(SIGN_EXTEND_I16_I8, MATCH(I<OPCODE_SIGN_EXTEND, I16<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsx(i.dest, i.src1);
  }
};
EMITTER(SIGN_EXTEND_I32_I8, MATCH(I<OPCODE_SIGN_EXTEND, I32<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsx(i.dest, i.src1);
  }
};
EMITTER(SIGN_EXTEND_I64_I8, MATCH(I<OPCODE_SIGN_EXTEND, I64<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsx(i.dest, i.src1);
  }
};
EMITTER(SIGN_EXTEND_I32_I16, MATCH(I<OPCODE_SIGN_EXTEND, I32<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsx(i.dest, i.src1);
  }
};
EMITTER(SIGN_EXTEND_I64_I16, MATCH(I<OPCODE_SIGN_EXTEND, I64<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsx(i.dest, i.src1);
  }
};
EMITTER(SIGN_EXTEND_I64_I32, MATCH(I<OPCODE_SIGN_EXTEND, I64<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsxd(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_SIGN_EXTEND,
    SIGN_EXTEND_I16_I8,
    SIGN_EXTEND_I32_I8,
    SIGN_EXTEND_I64_I8,
    SIGN_EXTEND_I32_I16,
    SIGN_EXTEND_I64_I16,
    SIGN_EXTEND_I64_I32);


// ============================================================================
// OPCODE_TRUNCATE
// ============================================================================
EMITTER(TRUNCATE_I8_I16, MATCH(I<OPCODE_TRUNCATE, I8<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest.reg().cvt32(), i.src1.reg().cvt8());
  }
};
EMITTER(TRUNCATE_I8_I32, MATCH(I<OPCODE_TRUNCATE, I8<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest.reg().cvt32(), i.src1.reg().cvt8());
  }
};
EMITTER(TRUNCATE_I8_I64, MATCH(I<OPCODE_TRUNCATE, I8<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest.reg().cvt32(), i.src1.reg().cvt8());
  }
};
EMITTER(TRUNCATE_I16_I32, MATCH(I<OPCODE_TRUNCATE, I16<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest.reg().cvt32(), i.src1.reg().cvt16());
  }
};
EMITTER(TRUNCATE_I16_I64, MATCH(I<OPCODE_TRUNCATE, I16<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest.reg().cvt32(), i.src1.reg().cvt16());
  }
};
EMITTER(TRUNCATE_I32_I64, MATCH(I<OPCODE_TRUNCATE, I32<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, i.src1.reg().cvt32());
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_TRUNCATE,
    TRUNCATE_I8_I16,
    TRUNCATE_I8_I32,
    TRUNCATE_I8_I64,
    TRUNCATE_I16_I32,
    TRUNCATE_I16_I64,
    TRUNCATE_I32_I64);


// ============================================================================
// OPCODE_CONVERT
// ============================================================================
EMITTER(CONVERT_I32_F32, MATCH(I<OPCODE_CONVERT, I32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): saturation check? cvtt* (trunc?)
    e.vcvtss2si(i.dest, i.src1);
  }
};
EMITTER(CONVERT_I32_F64, MATCH(I<OPCODE_CONVERT, I32<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): saturation check? cvtt* (trunc?)
    e.vcvttsd2si(i.dest, i.src1);
  }
};
EMITTER(CONVERT_I64_F64, MATCH(I<OPCODE_CONVERT, I64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): saturation check? cvtt* (trunc?)
    e.vcvttsd2si(i.dest, i.src1);
  }
};
EMITTER(CONVERT_F32_I32, MATCH(I<OPCODE_CONVERT, F32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): saturation check? cvtt* (trunc?)
    e.vcvtsi2ss(i.dest, i.src1);
  }
};
EMITTER(CONVERT_F32_F64, MATCH(I<OPCODE_CONVERT, F32<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): saturation check? cvtt* (trunc?)
    e.vcvtsd2ss(i.dest, i.src1);
  }
};
EMITTER(CONVERT_F64_I64, MATCH(I<OPCODE_CONVERT, F64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): saturation check? cvtt* (trunc?)
    e.vcvtsi2sd(i.dest, i.src1);
  }
};
EMITTER(CONVERT_F64_F32, MATCH(I<OPCODE_CONVERT, F64<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vcvtss2sd(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_CONVERT,
    CONVERT_I32_F32,
    CONVERT_I32_F64,
    CONVERT_I64_F64,
    CONVERT_F32_I32,
    CONVERT_F32_F64,
    CONVERT_F64_I64,
    CONVERT_F64_F32);


// ============================================================================
// OPCODE_ROUND
// ============================================================================
EMITTER(ROUND_F32, MATCH(I<OPCODE_ROUND, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case ROUND_TO_ZERO:
        e.vroundss(i.dest, i.src1, B00000011);
        break;
      case ROUND_TO_NEAREST:
        e.vroundss(i.dest, i.src1, B00000000);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.vroundss(i.dest, i.src1, B00000001);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.vroundss(i.dest, i.src1, B00000010);
        break;
    }
  }
};
EMITTER(ROUND_F64, MATCH(I<OPCODE_ROUND, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case ROUND_TO_ZERO:
        e.vroundsd(i.dest, i.src1, B00000011);
        break;
      case ROUND_TO_NEAREST:
        e.vroundsd(i.dest, i.src1, B00000000);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.vroundsd(i.dest, i.src1, B00000001);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.vroundsd(i.dest, i.src1, B00000010);
        break;
    }
  }
};
EMITTER(ROUND_V128, MATCH(I<OPCODE_ROUND, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case ROUND_TO_ZERO:
        e.vroundps(i.dest, i.src1, B00000011);
        break;
      case ROUND_TO_NEAREST:
        e.vroundps(i.dest, i.src1, B00000000);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.vroundps(i.dest, i.src1, B00000001);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.vroundps(i.dest, i.src1, B00000010);
        break;
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_ROUND,
    ROUND_F32,
    ROUND_F64,
    ROUND_V128);


// ============================================================================
// OPCODE_VECTOR_CONVERT_I2F
// ============================================================================
EMITTER(VECTOR_CONVERT_I2F, MATCH(I<OPCODE_VECTOR_CONVERT_I2F, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // flags = ARITHMETIC_UNSIGNED
    // TODO(benvanik): are these really the same? VC++ thinks so.
    e.vcvtdq2ps(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_VECTOR_CONVERT_I2F,
    VECTOR_CONVERT_I2F);


// ============================================================================
// OPCODE_VECTOR_CONVERT_F2I
// ============================================================================
EMITTER(VECTOR_CONVERT_F2I, MATCH(I<OPCODE_VECTOR_CONVERT_F2I, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // flags = ARITHMETIC_UNSIGNED | ARITHMETIC_UNSIGNED
    // TODO(benvanik): are these really the same? VC++ thinks so.
    e.vcvttps2dq(i.dest, i.src1);
    if (i.instr->flags & ARITHMETIC_SATURATE) {
      // TODO(benvanik): check saturation.
      // In theory cvt throws if it saturates.
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_VECTOR_CONVERT_F2I,
    VECTOR_CONVERT_F2I);


// ============================================================================
// OPCODE_LOAD_VECTOR_SHL
// ============================================================================
static vec128_t lvsl_table[17] = {
  vec128b( 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15),
  vec128b( 1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16),
  vec128b( 2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17),
  vec128b( 3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18),
  vec128b( 4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19),
  vec128b( 5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20),
  vec128b( 6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21),
  vec128b( 7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22),
  vec128b( 8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23),
  vec128b( 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24),
  vec128b(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25),
  vec128b(11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26),
  vec128b(12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
  vec128b(13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28),
  vec128b(14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29),
  vec128b(15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30),
  vec128b(16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31),
};
EMITTER(LOAD_VECTOR_SHL_I8, MATCH(I<OPCODE_LOAD_VECTOR_SHL, V128<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      auto sh = i.src1.constant();
      XEASSERT(sh < XECOUNT(lvsl_table));
      e.mov(e.rax, (uintptr_t)&lvsl_table[sh]);
      e.vmovaps(i.dest, e.ptr[e.rax]);
    } else {
#if XE_DEBUG
      // We should only ever be getting values in [0,16]. Assert that.
      Xbyak::Label skip;
      e.cmp(i.src1, 17);
      e.jb(skip);
      e.Trap();
      e.L(skip);
#endif  // XE_DEBUG
      // TODO(benvanik): find a cheaper way of doing this.
      e.movzx(e.rdx, i.src1);
      e.shl(e.rdx, 4);
      e.mov(e.rax, (uintptr_t)lvsl_table);
      e.vmovaps(i.dest, e.ptr[e.rax + e.rdx]);
      e.ReloadEDX();
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_LOAD_VECTOR_SHL,
    LOAD_VECTOR_SHL_I8);


// ============================================================================
// OPCODE_LOAD_VECTOR_SHR
// ============================================================================
static vec128_t lvsr_table[17] = {
  vec128b(16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31),
  vec128b(15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30),
  vec128b(14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29),
  vec128b(13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28),
  vec128b(12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
  vec128b(11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26),
  vec128b(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25),
  vec128b( 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24),
  vec128b( 8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23),
  vec128b( 7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22),
  vec128b( 6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21),
  vec128b( 5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20),
  vec128b( 4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19),
  vec128b( 3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18),
  vec128b( 2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17),
  vec128b( 1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16),
  vec128b( 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15),
};
EMITTER(LOAD_VECTOR_SHR_I8, MATCH(I<OPCODE_LOAD_VECTOR_SHR, V128<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      auto sh = i.src1.constant();
      XEASSERT(sh < XECOUNT(lvsr_table));
      e.mov(e.rax, (uintptr_t)&lvsr_table[sh]);
      e.vmovaps(i.dest, e.ptr[e.rax]);
    } else {
#if XE_DEBUG
      // We should only ever be getting values in [0,16]. Assert that.
      Xbyak::Label skip;
      e.cmp(i.src1, 17);
      e.jb(skip);
      e.Trap();
      e.L(skip);
#endif  // XE_DEBUG
      // TODO(benvanik): find a cheaper way of doing this.
      e.movzx(e.rdx, i.src1);
      e.shl(e.rdx, 4);
      e.mov(e.rax, (uintptr_t)lvsr_table);
      e.vmovaps(i.dest, e.ptr[e.rax + e.rdx]);
      e.ReloadEDX();
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_LOAD_VECTOR_SHR,
    LOAD_VECTOR_SHR_I8);


// ============================================================================
// OPCODE_LOAD_CLOCK
// ============================================================================
EMITTER(LOAD_CLOCK, MATCH(I<OPCODE_LOAD_CLOCK, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // It'd be cool to call QueryPerformanceCounter directly, but w/e.
    e.CallNative(LoadClock);
    e.mov(i.dest, e.rax);
  }
  static uint64_t LoadClock(void* raw_context) {
    LARGE_INTEGER counter;
    uint64_t time = 0;
    if (QueryPerformanceCounter(&counter)) {
      time = counter.QuadPart;
    }
    return time;
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_LOAD_CLOCK,
    LOAD_CLOCK);


// ============================================================================
// OPCODE_LOAD_LOCAL
// ============================================================================
// Note: all types are always aligned on the stack.
EMITTER(LOAD_LOCAL_I8, MATCH(I<OPCODE_LOAD_LOCAL, I8<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, e.byte[e.rsp + i.src1.constant()]);
    //e.TraceLoadI8(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
EMITTER(LOAD_LOCAL_I16, MATCH(I<OPCODE_LOAD_LOCAL, I16<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, e.word[e.rsp + i.src1.constant()]);
    //e.TraceLoadI16(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
EMITTER(LOAD_LOCAL_I32, MATCH(I<OPCODE_LOAD_LOCAL, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, e.dword[e.rsp + i.src1.constant()]);
    //e.TraceLoadI32(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
EMITTER(LOAD_LOCAL_I64, MATCH(I<OPCODE_LOAD_LOCAL, I64<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, e.qword[e.rsp + i.src1.constant()]);
    //e.TraceLoadI64(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
EMITTER(LOAD_LOCAL_F32, MATCH(I<OPCODE_LOAD_LOCAL, F32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovss(i.dest, e.dword[e.rsp + i.src1.constant()]);
    //e.TraceLoadF32(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
EMITTER(LOAD_LOCAL_F64, MATCH(I<OPCODE_LOAD_LOCAL, F64<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovsd(i.dest, e.qword[e.rsp + i.src1.constant()]);
    //e.TraceLoadF64(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
EMITTER(LOAD_LOCAL_V128, MATCH(I<OPCODE_LOAD_LOCAL, V128<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovaps(i.dest, e.ptr[e.rsp + i.src1.constant()]);
    //e.TraceLoadV128(DATA_LOCAL, i.src1.constant, i.dest);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_LOAD_LOCAL,
    LOAD_LOCAL_I8,
    LOAD_LOCAL_I16,
    LOAD_LOCAL_I32,
    LOAD_LOCAL_I64,
    LOAD_LOCAL_F32,
    LOAD_LOCAL_F64,
    LOAD_LOCAL_V128);


// ============================================================================
// OPCODE_STORE_LOCAL
// ============================================================================
// Note: all types are always aligned on the stack.
EMITTER(STORE_LOCAL_I8, MATCH(I<OPCODE_STORE_LOCAL, VoidOp, I32<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    //e.TraceStoreI8(DATA_LOCAL, i.src1.constant, i.src2);
    e.mov(e.byte[e.rsp + i.src1.constant()], i.src2);
  }
};
EMITTER(STORE_LOCAL_I16, MATCH(I<OPCODE_STORE_LOCAL, VoidOp, I32<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    //e.TraceStoreI16(DATA_LOCAL, i.src1.constant, i.src2);
    e.mov(e.word[e.rsp + i.src1.constant()], i.src2);
  }
};
EMITTER(STORE_LOCAL_I32, MATCH(I<OPCODE_STORE_LOCAL, VoidOp, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    //e.TraceStoreI32(DATA_LOCAL, i.src1.constant, i.src2);
    e.mov(e.dword[e.rsp + i.src1.constant()], i.src2);
  }
};
EMITTER(STORE_LOCAL_I64, MATCH(I<OPCODE_STORE_LOCAL, VoidOp, I32<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    //e.TraceStoreI64(DATA_LOCAL, i.src1.constant, i.src2);
    e.mov(e.qword[e.rsp + i.src1.constant()], i.src2);
  }
};
EMITTER(STORE_LOCAL_F32, MATCH(I<OPCODE_STORE_LOCAL, VoidOp, I32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    //e.TraceStoreF32(DATA_LOCAL, i.src1.constant, i.src2);
    e.vmovss(e.dword[e.rsp + i.src1.constant()], i.src2);
  }
};
EMITTER(STORE_LOCAL_F64, MATCH(I<OPCODE_STORE_LOCAL, VoidOp, I32<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    //e.TraceStoreF64(DATA_LOCAL, i.src1.constant, i.src2);
    e.vmovsd(e.qword[e.rsp + i.src1.constant()], i.src2);
  }
};
EMITTER(STORE_LOCAL_V128, MATCH(I<OPCODE_STORE_LOCAL, VoidOp, I32<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    //e.TraceStoreV128(DATA_LOCAL, i.src1.constant, i.src2);
    e.vmovaps(e.ptr[e.rsp + i.src1.constant()], i.src2);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_STORE_LOCAL,
    STORE_LOCAL_I8,
    STORE_LOCAL_I16,
    STORE_LOCAL_I32,
    STORE_LOCAL_I64,
    STORE_LOCAL_F32,
    STORE_LOCAL_F64,
    STORE_LOCAL_V128);


// ============================================================================
// OPCODE_LOAD_CONTEXT
// ============================================================================
// Note: all types are always aligned in the context.
RegExp ComputeContextAddress(X64Emitter& e, const OffsetOp& offset) {
  return e.rcx + offset.value;
}
EMITTER(LOAD_CONTEXT_I8, MATCH(I<OPCODE_LOAD_CONTEXT, I8<>, OffsetOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    e.mov(i.dest, e.byte[addr]);
    if (IsTracingData()) {
      e.mov(e.r8, e.byte[addr]);
      e.mov(e.rdx, i.src1.value);
      e.CallNative(TraceContextLoadI8);
    }
  }
};
EMITTER(LOAD_CONTEXT_I16, MATCH(I<OPCODE_LOAD_CONTEXT, I16<>, OffsetOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    e.mov(i.dest, e.word[addr]);
    if (IsTracingData()) {
      e.mov(e.r8, e.word[addr]);
      e.mov(e.rdx, i.src1.value);
      e.CallNative(TraceContextLoadI16);
    }
  }
};
EMITTER(LOAD_CONTEXT_I32, MATCH(I<OPCODE_LOAD_CONTEXT, I32<>, OffsetOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    e.mov(i.dest, e.dword[addr]);
    if (IsTracingData()) {
      e.mov(e.r8, e.dword[addr]);
      e.mov(e.rdx, i.src1.value);
      e.CallNative(TraceContextLoadI32);
    }
  }
};
EMITTER(LOAD_CONTEXT_I64, MATCH(I<OPCODE_LOAD_CONTEXT, I64<>, OffsetOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    e.mov(i.dest, e.qword[addr]);
    if (IsTracingData()) {
      e.mov(e.r8, e.qword[addr]);
      e.mov(e.rdx, i.src1.value);
      e.CallNative(TraceContextLoadI64);
    }
  }
};
EMITTER(LOAD_CONTEXT_F32, MATCH(I<OPCODE_LOAD_CONTEXT, F32<>, OffsetOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    e.vmovss(i.dest, e.dword[addr]);
    if (IsTracingData()) {
      e.lea(e.r8, e.dword[addr]);
      e.mov(e.rdx, i.src1.value);
      e.CallNative(TraceContextLoadF32);
    }
  }
};
EMITTER(LOAD_CONTEXT_F64, MATCH(I<OPCODE_LOAD_CONTEXT, F64<>, OffsetOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    e.vmovsd(i.dest, e.qword[addr]);
    if (IsTracingData()) {
      e.lea(e.r8, e.qword[addr]);
      e.mov(e.rdx, i.src1.value);
      e.CallNative(TraceContextLoadF64);
    }
  }
};
EMITTER(LOAD_CONTEXT_V128, MATCH(I<OPCODE_LOAD_CONTEXT, V128<>, OffsetOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    e.vmovaps(i.dest, e.ptr[addr]);
    if (IsTracingData()) {
      e.lea(e.r8, e.ptr[addr]);
      e.mov(e.rdx, i.src1.value);
      e.CallNative(TraceContextLoadV128);
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_LOAD_CONTEXT,
    LOAD_CONTEXT_I8,
    LOAD_CONTEXT_I16,
    LOAD_CONTEXT_I32,
    LOAD_CONTEXT_I64,
    LOAD_CONTEXT_F32,
    LOAD_CONTEXT_F64,
    LOAD_CONTEXT_V128);


// ============================================================================
// OPCODE_STORE_CONTEXT
// ============================================================================
// Note: all types are always aligned on the stack.
EMITTER(STORE_CONTEXT_I8, MATCH(I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.mov(e.byte[addr], i.src2.constant());
    } else {
      e.mov(e.byte[addr], i.src2);
    }
    if (IsTracingData()) {
      e.mov(e.r8, e.byte[addr]);
      e.mov(e.rdx, i.src1.value);
      e.CallNative(TraceContextStoreI8);
    }
  }
};
EMITTER(STORE_CONTEXT_I16, MATCH(I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.mov(e.word[addr], i.src2.constant());
    } else {
      e.mov(e.word[addr], i.src2);
    }
    if (IsTracingData()) {
      e.mov(e.r8, e.word[addr]);
      e.mov(e.rdx, i.src1.value);
      e.CallNative(TraceContextStoreI16);
    }
  }
};
EMITTER(STORE_CONTEXT_I32, MATCH(I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.mov(e.dword[addr], i.src2.constant());
    } else {
      e.mov(e.dword[addr], i.src2);
    }
    if (IsTracingData()) {
      e.mov(e.r8, e.dword[addr]);
      e.mov(e.rdx, i.src1.value);
      e.CallNative(TraceContextStoreI32);
    }
  }
};
EMITTER(STORE_CONTEXT_I64, MATCH(I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.MovMem64(addr, i.src2.constant());
    } else {
      e.mov(e.qword[addr], i.src2);
    }
    if (IsTracingData()) {
      e.mov(e.r8, e.qword[addr]);
      e.mov(e.rdx, i.src1.value);
      e.CallNative(TraceContextStoreI64);
    }
  }
};
EMITTER(STORE_CONTEXT_F32, MATCH(I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.mov(e.dword[addr], i.src2.value->constant.i32);
    } else {
      e.vmovss(e.dword[addr], i.src2);
    }
    if (IsTracingData()) {
      e.lea(e.r8, e.dword[addr]);
      e.mov(e.rdx, i.src1.value);
      e.CallNative(TraceContextStoreF32);
    }
  }
};
EMITTER(STORE_CONTEXT_F64, MATCH(I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.MovMem64(addr, i.src2.value->constant.i64);
    } else {
      e.vmovsd(e.qword[addr], i.src2);
    }
    if (IsTracingData()) {
      e.lea(e.r8, e.qword[addr]);
      e.mov(e.rdx, i.src1.value);
      e.CallNative(TraceContextStoreF64);
    }
  }
};
EMITTER(STORE_CONTEXT_V128, MATCH(I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      e.vmovaps(e.ptr[addr], e.xmm0);
    } else {
      e.vmovaps(e.ptr[addr], i.src2);
    }
    if (IsTracingData()) {
      e.lea(e.r8, e.ptr[addr]);
      e.mov(e.rdx, i.src1.value);
      e.CallNative(TraceContextStoreV128);
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_STORE_CONTEXT,
    STORE_CONTEXT_I8,
    STORE_CONTEXT_I16,
    STORE_CONTEXT_I32,
    STORE_CONTEXT_I64,
    STORE_CONTEXT_F32,
    STORE_CONTEXT_F64,
    STORE_CONTEXT_V128);


// ============================================================================
// OPCODE_LOAD
// ============================================================================
// Note: most *should* be aligned, but needs to be checked!
template <typename T>
bool CheckLoadAccessCallback(X64Emitter& e, const T& i) {
  // If this is a constant address load, check to see if it's in a
  // register range. We'll also probably want a dynamic check for
  // unverified stores. So far, most games use constants.
  if (!i.src1.is_constant) {
    return false;
  }
  uint64_t address = i.src1.constant() & 0xFFFFFFFF;
  auto cbs = e.runtime()->access_callbacks();
  while (cbs) {
    if (cbs->handles(cbs->context, address)) {
      e.mov(e.rcx, reinterpret_cast<uint64_t>(cbs->context));
      e.mov(e.rdx, address);
      e.CallNative(cbs->read);
      if (T::dest_type == KEY_TYPE_V_I8) {
        // No swap required.
        e.mov(i.dest, e.al);
      } else if (T::dest_type == KEY_TYPE_V_I16) {
        e.ror(e.ax, 8);
        e.mov(i.dest, e.ax);
      } else if (T::dest_type == KEY_TYPE_V_I32) {
        e.bswap(e.eax);
        e.mov(i.dest, e.eax);
      } else if (T::dest_type == KEY_TYPE_V_I64) {
        e.bswap(e.rax);
        e.mov(i.dest, e.rax);
      } else {
        XEASSERTALWAYS();
      }
      return true;
    }
    cbs = cbs->next;
  }
  return false;
}
template <typename T>
RegExp ComputeMemoryAddress(X64Emitter& e, const T& guest) {
  if (guest.is_constant) {
    // TODO(benvanik): figure out how to do this without a temp.
    // Since the constant is often 0x8... if we tried to use that as a
    // displacement it would be sign extended and mess things up.
    e.mov(e.eax, static_cast<uint32_t>(guest.constant()));
    return e.rdx + e.rax;
  } else {
    // Clear the top 32 bits, as they are likely garbage.
    // TODO(benvanik): find a way to avoid doing this.
    e.mov(e.eax, guest.reg().cvt32());
    return e.rdx + e.rax;
  }
}
uint64_t DynamicRegisterLoad(void* raw_context, uint32_t address) {
  auto thread_state = *((ThreadState**)raw_context);
  auto cbs = thread_state->runtime()->access_callbacks();
  while (cbs) {
    if (cbs->handles(cbs->context, address)) {
      return cbs->read(cbs->context, address);
    }
    cbs = cbs->next;
  }
  return 0;
}
void DynamicRegisterStore(void* raw_context, uint32_t address, uint64_t value) {
  auto thread_state = *((ThreadState**)raw_context);
  auto cbs = thread_state->runtime()->access_callbacks();
  while (cbs) {
    if (cbs->handles(cbs->context, address)) {
      cbs->write(cbs->context, address, value);
      return;
    }
    cbs = cbs->next;
  }
}
template <typename DEST_REG>
void EmitLoadCheck(X64Emitter& e, const I64<>& addr_value, DEST_REG& dest) {
  // rax = reserved
  // if (address >> 24 == 0x7F) call register load handler;
  auto addr = ComputeMemoryAddress(e, addr_value);
  e.lea(e.r8d, e.ptr[addr]);
  e.shr(e.r8d, 24);
  e.cmp(e.r8b, 0x7F);
  e.inLocalLabel();
  Xbyak::Label normal_addr;
  Xbyak::Label skip_load;
  e.jne(normal_addr);
  e.lea(e.rdx, e.ptr[addr]);
  e.CallNative(DynamicRegisterLoad);
  if (DEST_REG::key_type == KEY_TYPE_V_I32) {
    e.bswap(e.eax);
    e.mov(dest, e.eax);
  }
  e.jmp(skip_load);
  e.L(normal_addr);
  if (DEST_REG::key_type == KEY_TYPE_V_I32) {
    e.mov(dest, e.dword[addr]);
  }
  if (IsTracingData()) {
    e.mov(e.r8, dest);
    e.lea(e.rdx, e.ptr[addr]);
    if (DEST_REG::key_type == KEY_TYPE_V_I32) {
      e.CallNative(TraceMemoryLoadI32);
    } else if (DEST_REG::key_type == KEY_TYPE_V_I64) {
      e.CallNative(TraceMemoryLoadI64);
    }
  }
  e.L(skip_load);
  e.outLocalLabel();
}
template <typename SRC_REG>
void EmitStoreCheck(X64Emitter& e, const I64<>& addr_value, SRC_REG& src) {
  // rax = reserved
  // if (address >> 24 == 0x7F) call register store handler;
  auto addr = ComputeMemoryAddress(e, addr_value);
  e.lea(e.r8d, e.ptr[addr]);
  e.shr(e.r8d, 24);
  e.cmp(e.r8b, 0x7F);
  e.inLocalLabel();
  Xbyak::Label normal_addr;
  Xbyak::Label skip_load;
  e.jne(normal_addr);
  e.lea(e.rdx, e.ptr[addr]);
  if (SRC_REG::key_type == KEY_TYPE_V_I32) {
    if (src.is_constant) {
      e.mov(e.r8d, XESWAP32(static_cast<uint32_t>(src.constant())));
    } else {
      e.mov(e.r8d, src);
      e.bswap(e.r8d);
    }
  } else if (SRC_REG::key_type == KEY_TYPE_V_I64) {
    if (src.is_constant) {
      e.mov(e.r8, XESWAP64(static_cast<uint64_t>(src.constant())));
    } else {
      e.mov(e.r8, src);
      e.bswap(e.r8);
    }
  }
  e.CallNative(DynamicRegisterStore);
  e.jmp(skip_load);
  e.L(normal_addr);
  if (SRC_REG::key_type == KEY_TYPE_V_I32) {
    if (src.is_constant) {
      e.mov(e.dword[addr], src.constant());
    } else {
      e.mov(e.dword[addr], src);
    }
  } else if (SRC_REG::key_type == KEY_TYPE_V_I64) {
    if (src.is_constant) {
      e.MovMem64(addr, src.constant());
    } else {
      e.mov(e.qword[addr], src);
    }
  }
  if (IsTracingData()) {
    e.mov(e.r8, e.qword[addr]);
    e.lea(e.rdx, e.ptr[addr]);
    if (SRC_REG::key_type == KEY_TYPE_V_I32) {
      e.CallNative(TraceMemoryStoreI32);
    } else if (SRC_REG::key_type == KEY_TYPE_V_I64) {
      e.CallNative(TraceMemoryStoreI64);
    }
  }
  e.L(skip_load);
  e.outLocalLabel();
}
EMITTER(LOAD_I8, MATCH(I<OPCODE_LOAD, I8<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (CheckLoadAccessCallback(e, i)) {
      return;
    }
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.mov(i.dest, e.byte[addr]);
    if (IsTracingData()) {
      e.mov(e.r8, i.dest);
      e.lea(e.rdx, e.ptr[addr]);
      e.CallNative(TraceMemoryLoadI8);
    }
  }
};
EMITTER(LOAD_I16, MATCH(I<OPCODE_LOAD, I16<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (CheckLoadAccessCallback(e, i)) {
      return;
    }
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.mov(i.dest, e.word[addr]);
    if (IsTracingData()) {
      e.mov(e.r8, i.dest);
      e.lea(e.rdx, e.ptr[addr]);
      e.CallNative(TraceMemoryLoadI16);
    }
  }
};
EMITTER(LOAD_I32, MATCH(I<OPCODE_LOAD, I32<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (CheckLoadAccessCallback(e, i)) {
      return;
    }
    EmitLoadCheck(e, i.src1, i.dest);
  }
};
EMITTER(LOAD_I64, MATCH(I<OPCODE_LOAD, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (CheckLoadAccessCallback(e, i)) {
      return;
    }
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.mov(i.dest, e.qword[addr]);
    if (IsTracingData()) {
      e.mov(e.r8, i.dest);
      e.lea(e.rdx, e.ptr[addr]);
      e.CallNative(TraceMemoryLoadI64);
    }
  }
};
EMITTER(LOAD_F32, MATCH(I<OPCODE_LOAD, F32<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.vmovss(i.dest, e.dword[addr]);
    if (IsTracingData()) {
      e.lea(e.r8, e.dword[addr]);
      e.lea(e.rdx, e.ptr[addr]);
      e.CallNative(TraceMemoryLoadF32);
    }
  }
};
EMITTER(LOAD_F64, MATCH(I<OPCODE_LOAD, F64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.vmovsd(i.dest, e.qword[addr]);
    if (IsTracingData()) {
      e.lea(e.r8, e.qword[addr]);
      e.lea(e.rdx, e.ptr[addr]);
      e.CallNative(TraceMemoryLoadF64);
    }
  }
};
EMITTER(LOAD_V128, MATCH(I<OPCODE_LOAD, V128<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    // TODO(benvanik): we should try to stick to movaps if possible.
    e.vmovups(i.dest, e.ptr[addr]);
    if (IsTracingData()) {
      e.lea(e.r8, e.ptr[addr]);
      e.lea(e.rdx, e.ptr[addr]);
      e.CallNative(TraceMemoryLoadV128);
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_LOAD,
    LOAD_I8,
    LOAD_I16,
    LOAD_I32,
    LOAD_I64,
    LOAD_F32,
    LOAD_F64,
    LOAD_V128);


// ============================================================================
// OPCODE_STORE
// ============================================================================
// Note: most *should* be aligned, but needs to be checked!
template <typename T>
bool CheckStoreAccessCallback(X64Emitter& e, const T& i) {
  // If this is a constant address store, check to see if it's in a
  // register range. We'll also probably want a dynamic check for
  // unverified stores. So far, most games use constants.
  if (!i.src1.is_constant) {
    return false;
  }
  uint64_t address = i.src1.constant() & 0xFFFFFFFF;
  auto cbs = e.runtime()->access_callbacks();
  while (cbs) {
    if (cbs->handles(cbs->context, address)) {
      e.mov(e.rcx, reinterpret_cast<uint64_t>(cbs->context));
      e.mov(e.rdx, address);
      if (i.src2.is_constant) {
        e.mov(e.r8, i.src2.constant());
      } else {
        if (T::src2_type == KEY_TYPE_V_I8) {
          // No swap required.
          e.movzx(e.r8, i.src2.reg().cvt8());
        } else if (T::src2_type == KEY_TYPE_V_I16) {
          e.movzx(e.r8, i.src2.reg().cvt16());
          e.ror(e.r8w, 8);
        } else if (T::src2_type == KEY_TYPE_V_I32) {
          e.mov(e.r8d, i.src2.reg().cvt32());
          e.bswap(e.r8d);
        } else if (T::src2_type == KEY_TYPE_V_I64) {
          e.mov(e.r8, i.src2);
          e.bswap(e.r8);
        } else {
          XEASSERTALWAYS();
        }
      }
      e.CallNative(cbs->write);
      return true;
    }
    cbs = cbs->next;
  }
  return false;
}
EMITTER(STORE_I8, MATCH(I<OPCODE_STORE, VoidOp, I64<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (CheckStoreAccessCallback(e, i)) {
      return;
    }
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.mov(e.byte[addr], i.src2.constant());
    } else {
      e.mov(e.byte[addr], i.src2);
    }
    if (IsTracingData()) {
      e.mov(e.r8, e.byte[addr]);
      e.lea(e.rdx, e.ptr[addr]);
      e.CallNative(TraceMemoryStoreI8);
    }
  }
};
EMITTER(STORE_I16, MATCH(I<OPCODE_STORE, VoidOp, I64<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (CheckStoreAccessCallback(e, i)) {
      return;
    }
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.mov(e.word[addr], i.src2.constant());
    } else {
      e.mov(e.word[addr], i.src2);
    }
    if (IsTracingData()) {
      e.mov(e.r8, e.word[addr]);
      e.lea(e.rdx, e.ptr[addr]);
      e.CallNative(TraceMemoryStoreI16);
    }
  }
};
EMITTER(STORE_I32, MATCH(I<OPCODE_STORE, VoidOp, I64<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (CheckStoreAccessCallback(e, i)) {
      return;
    }
    EmitStoreCheck(e, i.src1, i.src2);
  }
};
EMITTER(STORE_I64, MATCH(I<OPCODE_STORE, VoidOp, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (CheckStoreAccessCallback(e, i)) {
      return;
    }
    EmitStoreCheck(e, i.src1, i.src2);
  }
};
EMITTER(STORE_F32, MATCH(I<OPCODE_STORE, VoidOp, I64<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.mov(e.dword[addr], i.src2.value->constant.i32);
    } else {
      e.vmovss(e.dword[addr], i.src2);
    }
    if (IsTracingData()) {
      e.lea(e.r8, e.ptr[addr]);
      e.lea(e.rdx, e.ptr[addr]);
      e.CallNative(TraceMemoryStoreF32);
    }
  }
};
EMITTER(STORE_F64, MATCH(I<OPCODE_STORE, VoidOp, I64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.MovMem64(addr, i.src2.value->constant.i64);
    } else {
      e.vmovsd(e.qword[addr], i.src2);
    }
    if (IsTracingData()) {
      e.lea(e.r8, e.ptr[addr]);
      e.lea(e.rdx, e.ptr[addr]);
      e.CallNative(TraceMemoryStoreF64);
    }
  }
};
EMITTER(STORE_V128, MATCH(I<OPCODE_STORE, VoidOp, I64<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      e.vmovaps(e.ptr[addr], e.xmm0);
    } else {
      e.vmovaps(e.ptr[addr], i.src2);
    }
    if (IsTracingData()) {
      e.lea(e.r8, e.ptr[addr]);
      e.lea(e.rdx, e.ptr[addr]);
      e.CallNative(TraceMemoryStoreV128);
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_STORE,
    STORE_I8,
    STORE_I16,
    STORE_I32,
    STORE_I64,
    STORE_F32,
    STORE_F64,
    STORE_V128);


// ============================================================================
// OPCODE_PREFETCH
// ============================================================================
EMITTER(PREFETCH, MATCH(I<OPCODE_PREFETCH, VoidOp, I64<>, OffsetOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): prefetch addr -> length.
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_PREFETCH,
    PREFETCH);


// ============================================================================
// OPCODE_MAX
// ============================================================================
EMITTER(MAX_F32, MATCH(I<OPCODE_MAX, F32<>, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vmaxss(dest, src1, src2);
        });
  }
};
EMITTER(MAX_F64, MATCH(I<OPCODE_MAX, F64<>, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vmaxsd(dest, src1, src2);
        });
  }
};
EMITTER(MAX_V128, MATCH(I<OPCODE_MAX, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vmaxps(dest, src1, src2);
        });
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_MAX,
    MAX_F32,
    MAX_F64,
    MAX_V128);


// ============================================================================
// OPCODE_MIN
// ============================================================================
EMITTER(MIN_F32, MATCH(I<OPCODE_MIN, F32<>, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vminss(dest, src1, src2);
        });
  }
};
EMITTER(MIN_F64, MATCH(I<OPCODE_MIN, F64<>, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vminsd(dest, src1, src2);
        });
  }
};
EMITTER(MIN_V128, MATCH(I<OPCODE_MIN, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vminps(dest, src1, src2);
        });
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_MIN,
    MIN_F32,
    MIN_F64,
    MIN_V128);


// ============================================================================
// OPCODE_SELECT
// ============================================================================
// dest = src1 ? src2 : src3
// TODO(benvanik): match compare + select sequences, as often it's something
//     like SELECT(VECTOR_COMPARE_SGE(a, b), a, b)
EMITTER(SELECT_I8, MATCH(I<OPCODE_SELECT, I8<>, I8<>, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.cmovnz(i.dest.reg().cvt32(), i.src2.reg().cvt32());
    e.cmovz(i.dest.reg().cvt32(), i.src3.reg().cvt32());
  }
};
EMITTER(SELECT_I16, MATCH(I<OPCODE_SELECT, I16<>, I8<>, I16<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.cmovnz(i.dest.reg().cvt32(), i.src2.reg().cvt32());
    e.cmovz(i.dest.reg().cvt32(), i.src3.reg().cvt32());
  }
};
EMITTER(SELECT_I32, MATCH(I<OPCODE_SELECT, I32<>, I8<>, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.cmovnz(i.dest, i.src2);
    e.cmovz(i.dest, i.src3);
  }
};
EMITTER(SELECT_I64, MATCH(I<OPCODE_SELECT, I64<>, I8<>, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.cmovnz(i.dest, i.src2);
    e.cmovz(i.dest, i.src3);
  }
};
EMITTER(SELECT_F32, MATCH(I<OPCODE_SELECT, F32<>, I8<>, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): find a shorter sequence.
    // xmm0 = src1 != 0 ? 1111... : 0000....
    e.movzx(e.eax, i.src1);
    e.vmovd(e.xmm1, e.eax);
    e.vxorps(e.xmm0, e.xmm0);
    e.vcmpneqss(e.xmm0, e.xmm1);
    e.vpand(e.xmm1, e.xmm0, i.src2);
    e.vpandn(i.dest, e.xmm0, i.src3);
    e.vpor(i.dest, e.xmm1);
  }
};
EMITTER(SELECT_F64, MATCH(I<OPCODE_SELECT, F64<>, I8<>, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // xmm0 = src1 != 0 ? 1111... : 0000....
    e.movzx(e.eax, i.src1);
    e.vmovd(e.xmm1, e.eax);
    e.vxorpd(e.xmm0, e.xmm0);
    e.vcmpneqsd(e.xmm0, e.xmm1);
    e.vpand(e.xmm1, e.xmm0, i.src2);
    e.vpandn(i.dest, e.xmm0, i.src3);
    e.vpor(i.dest, e.xmm1);
  }
};
EMITTER(SELECT_V128, MATCH(I<OPCODE_SELECT, V128<>, I8<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): find a shorter sequence.
    // xmm0 = src1 != 0 ? 1111... : 0000....
    e.movzx(e.eax, i.src1);
    e.vmovd(e.xmm1, e.eax);
    e.vpbroadcastd(e.xmm1, e.xmm1);
    e.vxorps(e.xmm0, e.xmm0);
    e.vcmpneqps(e.xmm0, e.xmm1);
    e.vpand(e.xmm1, e.xmm0, i.src2);
    e.vpandn(i.dest, e.xmm0, i.src3);
    e.vpor(i.dest, e.xmm1);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_SELECT,
    SELECT_I8,
    SELECT_I16,
    SELECT_I32,
    SELECT_I64,
    SELECT_F32,
    SELECT_F64,
    SELECT_V128);


// ============================================================================
// OPCODE_IS_TRUE
// ============================================================================
EMITTER(IS_TRUE_I8, MATCH(I<OPCODE_IS_TRUE, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
EMITTER(IS_TRUE_I16, MATCH(I<OPCODE_IS_TRUE, I8<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
EMITTER(IS_TRUE_I32, MATCH(I<OPCODE_IS_TRUE, I8<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
EMITTER(IS_TRUE_I64, MATCH(I<OPCODE_IS_TRUE, I8<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
EMITTER(IS_TRUE_F32, MATCH(I<OPCODE_IS_TRUE, I8<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
EMITTER(IS_TRUE_F64, MATCH(I<OPCODE_IS_TRUE, I8<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
EMITTER(IS_TRUE_V128, MATCH(I<OPCODE_IS_TRUE, I8<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_IS_TRUE,
    IS_TRUE_I8,
    IS_TRUE_I16,
    IS_TRUE_I32,
    IS_TRUE_I64,
    IS_TRUE_F32,
    IS_TRUE_F64,
    IS_TRUE_V128);


// ============================================================================
// OPCODE_IS_FALSE
// ============================================================================
EMITTER(IS_FALSE_I8, MATCH(I<OPCODE_IS_FALSE, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setz(i.dest);
  }
};
EMITTER(IS_FALSE_I16, MATCH(I<OPCODE_IS_FALSE, I8<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setz(i.dest);
  }
};
EMITTER(IS_FALSE_I32, MATCH(I<OPCODE_IS_FALSE, I8<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setz(i.dest);
  }
};
EMITTER(IS_FALSE_I64, MATCH(I<OPCODE_IS_FALSE, I8<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setz(i.dest);
  }
};
EMITTER(IS_FALSE_F32, MATCH(I<OPCODE_IS_FALSE, I8<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setz(i.dest);
  }
};
EMITTER(IS_FALSE_F64, MATCH(I<OPCODE_IS_FALSE, I8<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setz(i.dest);
  }
};
EMITTER(IS_FALSE_V128, MATCH(I<OPCODE_IS_FALSE, I8<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setz(i.dest);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_IS_FALSE,
    IS_FALSE_I8,
    IS_FALSE_I16,
    IS_FALSE_I32,
    IS_FALSE_I64,
    IS_FALSE_F32,
    IS_FALSE_F64,
    IS_FALSE_V128);


// ============================================================================
// OPCODE_COMPARE_EQ
// ============================================================================
EMITTER(COMPARE_EQ_I8, MATCH(I<OPCODE_COMPARE_EQ, I8<>, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(
        e, i,
        [](X64Emitter& e, const Reg8& src1, const Reg8& src2) { e.cmp(src1, src2); },
        [](X64Emitter& e, const Reg8& src1, int32_t constant) { e.cmp(src1, constant); });
    e.sete(i.dest);
  }
};
EMITTER(COMPARE_EQ_I16, MATCH(I<OPCODE_COMPARE_EQ, I8<>, I16<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(
        e, i,
        [](X64Emitter& e, const Reg16& src1, const Reg16& src2) { e.cmp(src1, src2); },
        [](X64Emitter& e, const Reg16& src1, int32_t constant) { e.cmp(src1, constant); });
    e.sete(i.dest);
  }
};
EMITTER(COMPARE_EQ_I32, MATCH(I<OPCODE_COMPARE_EQ, I8<>, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(
        e, i,
        [](X64Emitter& e, const Reg32& src1, const Reg32& src2) { e.cmp(src1, src2); },
        [](X64Emitter& e, const Reg32& src1, int32_t constant) { e.cmp(src1, constant); });
    e.sete(i.dest);
  }
};
EMITTER(COMPARE_EQ_I64, MATCH(I<OPCODE_COMPARE_EQ, I8<>, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(
        e, i,
        [](X64Emitter& e, const Reg64& src1, const Reg64& src2) { e.cmp(src1, src2); },
        [](X64Emitter& e, const Reg64& src1, int32_t constant) { e.cmp(src1, constant); });
    e.sete(i.dest);
  }
};
EMITTER(COMPARE_EQ_F32, MATCH(I<OPCODE_COMPARE_EQ, I8<>, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vcomiss(i.src1, i.src2);
    e.sete(i.dest);
  }
};
EMITTER(COMPARE_EQ_F64, MATCH(I<OPCODE_COMPARE_EQ, I8<>, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vcomisd(i.src1, i.src2);
    e.sete(i.dest);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_COMPARE_EQ,
    COMPARE_EQ_I8,
    COMPARE_EQ_I16,
    COMPARE_EQ_I32,
    COMPARE_EQ_I64,
    COMPARE_EQ_F32,
    COMPARE_EQ_F64);


// ============================================================================
// OPCODE_COMPARE_NE
// ============================================================================
EMITTER(COMPARE_NE_I8, MATCH(I<OPCODE_COMPARE_NE, I8<>, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(
        e, i,
        [](X64Emitter& e, const Reg8& src1, const Reg8& src2) { e.cmp(src1, src2); },
        [](X64Emitter& e, const Reg8& src1, int32_t constant) { e.cmp(src1, constant); });
    e.setne(i.dest);
  }
};
EMITTER(COMPARE_NE_I16, MATCH(I<OPCODE_COMPARE_NE, I8<>, I16<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(
        e, i,
        [](X64Emitter& e, const Reg16& src1, const Reg16& src2) { e.cmp(src1, src2); },
        [](X64Emitter& e, const Reg16& src1, int32_t constant) { e.cmp(src1, constant); });
    e.setne(i.dest);
  }
};
EMITTER(COMPARE_NE_I32, MATCH(I<OPCODE_COMPARE_NE, I8<>, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(
        e, i,
        [](X64Emitter& e, const Reg32& src1, const Reg32& src2) { e.cmp(src1, src2); },
        [](X64Emitter& e, const Reg32& src1, int32_t constant) { e.cmp(src1, constant); });
    e.setne(i.dest);
  }
};
EMITTER(COMPARE_NE_I64, MATCH(I<OPCODE_COMPARE_NE, I8<>, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(
        e, i,
        [](X64Emitter& e, const Reg64& src1, const Reg64& src2) { e.cmp(src1, src2); },
        [](X64Emitter& e, const Reg64& src1, int32_t constant) { e.cmp(src1, constant); });
    e.setne(i.dest);
  }
};
EMITTER(COMPARE_NE_F32, MATCH(I<OPCODE_COMPARE_NE, I8<>, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vcomiss(i.src1, i.src2);
    e.setne(i.dest);
  }
};
EMITTER(COMPARE_NE_F64, MATCH(I<OPCODE_COMPARE_NE, I8<>, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vcomisd(i.src1, i.src2);
    e.setne(i.dest);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_COMPARE_NE,
    COMPARE_NE_I8,
    COMPARE_NE_I16,
    COMPARE_NE_I32,
    COMPARE_NE_I64,
    COMPARE_NE_F32,
    COMPARE_NE_F64);


// ============================================================================
// OPCODE_COMPARE_*
// ============================================================================
#define EMITTER_ASSOCIATIVE_COMPARE_INT(op, instr, inverse_instr, type, reg_type) \
    EMITTER(COMPARE_##op##_##type, MATCH(I<OPCODE_COMPARE_##op##, I8<>, type<>, type<>>)) { \
        static void Emit(X64Emitter& e, const EmitArgType& i) { \
          EmitAssociativeCompareOp( \
              e, i, \
              [](X64Emitter& e, const Reg8& dest, const reg_type& src1, const reg_type& src2, bool inverse) { \
                  e.cmp(src1, src2); \
                  if (!inverse) { e.instr(dest); } else { e.inverse_instr(dest); } \
              }, \
              [](X64Emitter& e, const Reg8& dest, const reg_type& src1, int32_t constant, bool inverse) { \
                  e.cmp(src1, constant); \
                  if (!inverse) { e.instr(dest); } else { e.inverse_instr(dest); } \
              }); \
        } \
    };
#define EMITTER_ASSOCIATIVE_COMPARE_XX(op, instr, inverse_instr) \
    EMITTER_ASSOCIATIVE_COMPARE_INT(op, instr, inverse_instr, I8, Reg8); \
    EMITTER_ASSOCIATIVE_COMPARE_INT(op, instr, inverse_instr, I16, Reg16); \
    EMITTER_ASSOCIATIVE_COMPARE_INT(op, instr, inverse_instr, I32, Reg32); \
    EMITTER_ASSOCIATIVE_COMPARE_INT(op, instr, inverse_instr, I64, Reg64); \
    EMITTER_OPCODE_TABLE( \
        OPCODE_COMPARE_##op##, \
        COMPARE_##op##_I8, \
        COMPARE_##op##_I16, \
        COMPARE_##op##_I32, \
        COMPARE_##op##_I64);
EMITTER_ASSOCIATIVE_COMPARE_XX(SLT, setl, setge);
EMITTER_ASSOCIATIVE_COMPARE_XX(SLE, setle, setg);
EMITTER_ASSOCIATIVE_COMPARE_XX(SGT, setg, setle);
EMITTER_ASSOCIATIVE_COMPARE_XX(SGE, setge, setl);
EMITTER_ASSOCIATIVE_COMPARE_XX(ULT, setb, setae);
EMITTER_ASSOCIATIVE_COMPARE_XX(ULE, setbe, seta);
EMITTER_ASSOCIATIVE_COMPARE_XX(UGT, seta, setbe);
EMITTER_ASSOCIATIVE_COMPARE_XX(UGE, setae, setb);

// http://x86.renejeschke.de/html/file_module_x86_id_288.html
#define EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(op, instr) \
    EMITTER(COMPARE_##op##_F32, MATCH(I<OPCODE_COMPARE_##op##, I8<>, F32<>, F32<>>)) { \
      static void Emit(X64Emitter& e, const EmitArgType& i) { \
        e.vcomiss(i.src1, i.src2); \
        e.instr(i.dest); \
      } \
    }; \
    EMITTER(COMPARE_##op##_F64, MATCH(I<OPCODE_COMPARE_##op##, I8<>, F64<>, F64<>>)) { \
      static void Emit(X64Emitter& e, const EmitArgType& i) { \
        if (i.src1.is_constant) { \
          e.LoadConstantXmm(e.xmm0, i.src1.constant()); \
          e.vcomisd(e.xmm0, i.src2); \
        } else if (i.src2.is_constant) { \
          e.LoadConstantXmm(e.xmm0, i.src2.constant()); \
          e.vcomisd(i.src1, e.xmm0); \
        } else { \
          e.vcomisd(i.src1, i.src2); \
        } \
        e.instr(i.dest); \
      } \
    }; \
    EMITTER_OPCODE_TABLE( \
        OPCODE_COMPARE_##op##_FLT, \
        COMPARE_##op##_F32, \
        COMPARE_##op##_F64);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(SLT, setb);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(SLE, setbe);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(SGT, seta);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(SGE, setae);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(ULT, setb);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(ULE, setbe);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(UGT, seta);
EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(UGE, setae);


// ============================================================================
// OPCODE_DID_CARRY
// ============================================================================
// TODO(benvanik): salc/setalc
// https://code.google.com/p/corkami/wiki/x86oddities
EMITTER(DID_CARRY_I8, MATCH(I<OPCODE_DID_CARRY, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERT(!i.src1.is_constant);
    e.LoadEflags();
    e.setc(i.dest);
  }
};
EMITTER(DID_CARRY_I16, MATCH(I<OPCODE_DID_CARRY, I8<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERT(!i.src1.is_constant);
    e.LoadEflags();
    e.setc(i.dest);
  }
};
EMITTER(DID_CARRY_I32, MATCH(I<OPCODE_DID_CARRY, I8<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERT(!i.src1.is_constant);
    e.LoadEflags();
    e.setc(i.dest);
  }
};
EMITTER(DID_CARRY_I64, MATCH(I<OPCODE_DID_CARRY, I8<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERT(!i.src1.is_constant);
    e.LoadEflags();
    e.setc(i.dest);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_DID_CARRY,
    DID_CARRY_I8,
    DID_CARRY_I16,
    DID_CARRY_I32,
    DID_CARRY_I64);


// ============================================================================
// OPCODE_DID_OVERFLOW
// ============================================================================
EMITTER(DID_OVERFLOW, MATCH(I<OPCODE_DID_OVERFLOW, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.LoadEflags();
    e.seto(i.dest);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_DID_OVERFLOW,
    DID_OVERFLOW);


// ============================================================================
// OPCODE_DID_SATURATE
// ============================================================================
EMITTER(DID_SATURATE, MATCH(I<OPCODE_DID_SATURATE, I8<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): implement saturation check (VECTOR_ADD, etc).
    e.xor(i.dest, i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DID_SATURATE,
                     DID_SATURATE);


// ============================================================================
// OPCODE_VECTOR_COMPARE_EQ
// ============================================================================
EMITTER(VECTOR_COMPARE_EQ_V128, MATCH(I<OPCODE_VECTOR_COMPARE_EQ, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
        [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
      switch (i.instr->flags) {
      case INT8_TYPE:
        e.vpcmpeqb(dest, src1, src2);
        break;
      case INT16_TYPE:
        e.vpcmpeqw(dest, src1, src2);
        break;
      case INT32_TYPE:
        e.vpcmpeqd(dest, src1, src2);
        break;
      case FLOAT32_TYPE:
        e.vcmpeqps(dest, src1, src2);
        break;
      }
    });
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_VECTOR_COMPARE_EQ,
    VECTOR_COMPARE_EQ_V128);


// ============================================================================
// OPCODE_VECTOR_COMPARE_SGT
// ============================================================================
EMITTER(VECTOR_COMPARE_SGT_V128, MATCH(I<OPCODE_VECTOR_COMPARE_SGT, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAssociativeBinaryXmmOp(e, i,
        [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
      switch (i.instr->flags) {
      case INT8_TYPE:
        e.vpcmpgtb(dest, src1, src2);
        break;
      case INT16_TYPE:
        e.vpcmpgtw(dest, src1, src2);
        break;
      case INT32_TYPE:
        e.vpcmpgtd(dest, src1, src2);
        break;
      case FLOAT32_TYPE:
        e.vcmpgtps(dest, src1, src2);
        break;
      }
    });
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_VECTOR_COMPARE_SGT,
    VECTOR_COMPARE_SGT_V128);


// ============================================================================
// OPCODE_VECTOR_COMPARE_SGE
// ============================================================================
EMITTER(VECTOR_COMPARE_SGE_V128, MATCH(I<OPCODE_VECTOR_COMPARE_SGE, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAssociativeBinaryXmmOp(e, i,
        [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
      switch (i.instr->flags) {
      case INT8_TYPE:
        e.vpcmpeqb(e.xmm0, src1, src2);
        e.vpcmpgtb(dest, src1, src2);
        e.vpor(dest, e.xmm0);
        break;
      case INT16_TYPE:
        e.vpcmpeqw(e.xmm0, src1, src2);
        e.vpcmpgtw(dest, src1, src2);
        e.vpor(dest, e.xmm0);
        break;
      case INT32_TYPE:
        e.vpcmpeqd(e.xmm0, src1, src2);
        e.vpcmpgtd(dest, src1, src2);
        e.vpor(dest, e.xmm0);
        break;
      case FLOAT32_TYPE:
        e.vcmpgeps(dest, src1, src2);
        break;
      }
    });
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_VECTOR_COMPARE_SGE,
    VECTOR_COMPARE_SGE_V128);


// ============================================================================
// OPCODE_VECTOR_COMPARE_UGT
// ============================================================================
//EMITTER(VECTOR_COMPARE_UGT_V128, MATCH(I<OPCODE_VECTOR_COMPARE_UGT, V128<>, V128<>, V128<>>)) {
//  static void Emit(X64Emitter& e, const EmitArgType& i) {
//  }
//};
//EMITTER_OPCODE_TABLE(
//    OPCODE_VECTOR_COMPARE_UGT,
//    VECTOR_COMPARE_UGT_V128);


// ============================================================================
// OPCODE_VECTOR_COMPARE_UGE
// ============================================================================
//EMITTER(VECTOR_COMPARE_UGE_V128, MATCH(I<OPCODE_VECTOR_COMPARE_UGE, V128<>, V128<>, V128<>>)) {
//  static void Emit(X64Emitter& e, const EmitArgType& i) {
//  }
//};
//EMITTER_OPCODE_TABLE(
//    OPCODE_VECTOR_COMPARE_UGE,
//    VECTOR_COMPARE_UGE_V128);


// ============================================================================
// OPCODE_ADD
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitAddXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitCommutativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const REG& src) { e.add(dest_src, src); },
      [](X64Emitter& e, const REG& dest_src, int32_t constant) { e.add(dest_src, constant); });
  if (i.instr->flags & ARITHMETIC_SET_CARRY) {
    // CF is set if carried.
    e.StoreEflags();
  }
}
EMITTER(ADD_I8, MATCH(I<OPCODE_ADD, I8<>, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddXX<ADD_I8, Reg8>(e, i);
  }
};
EMITTER(ADD_I16, MATCH(I<OPCODE_ADD, I16<>, I16<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddXX<ADD_I16, Reg16>(e, i);
  }
};
EMITTER(ADD_I32, MATCH(I<OPCODE_ADD, I32<>, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddXX<ADD_I32, Reg32>(e, i);
  }
};
EMITTER(ADD_I64, MATCH(I<OPCODE_ADD, I64<>, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddXX<ADD_I64, Reg64>(e, i);
  }
};
EMITTER(ADD_F32, MATCH(I<OPCODE_ADD, F32<>, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vaddss(dest, src1, src2);
        });
  }
};
EMITTER(ADD_F64, MATCH(I<OPCODE_ADD, F64<>, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vaddsd(dest, src1, src2);
        });
  }
};
EMITTER(ADD_V128, MATCH(I<OPCODE_ADD, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vaddps(dest, src1, src2);
        });
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_ADD,
    ADD_I8,
    ADD_I16,
    ADD_I32,
    ADD_I64,
    ADD_F32,
    ADD_F64,
    ADD_V128);


// ============================================================================
// OPCODE_ADD_CARRY
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitAddCarryXX(X64Emitter& e, const ARGS& i) {
  // TODO(benvanik): faster setting? we could probably do some fun math tricks
  // here to get the carry flag set.
  if (i.src3.is_constant) {
    if (i.src3.constant()) {
      e.stc();
    } else {
      e.clc();
    }
  } else {
    if (i.src3.reg().getIdx() <= 4) {
      // Can move from A/B/C/DX to AH.
      e.mov(e.ah, i.src3.reg().cvt8());
    } else {
      e.mov(e.al, i.src3);
      e.mov(e.ah, e.al);
    }
    e.sahf();
  }
  if (i.src1.is_constant && i.src2.is_constant) {
    auto ab = i.src1.constant() + i.src2.constant();
    if (!ab) {
      e.xor(i.dest, i.dest);
    } else {
      e.mov(i.dest, ab);
    }
    e.adc(i.dest, 0);
  } else {
    SEQ::EmitCommutativeBinaryOp(
      e, i, [](X64Emitter& e, const REG& dest_src, const REG& src) {
      e.adc(dest_src, src);
    }, [](X64Emitter& e, const REG& dest_src, int32_t constant) {
      e.adc(dest_src, constant);
    });
  }
  if (i.instr->flags & ARITHMETIC_SET_CARRY) {
    // CF is set if carried.
    e.StoreEflags();
  }
}
EMITTER(ADD_CARRY_I8, MATCH(I<OPCODE_ADD_CARRY, I8<>, I8<>, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddCarryXX<ADD_CARRY_I8, Reg8>(e, i);
  }
};
EMITTER(ADD_CARRY_I16, MATCH(I<OPCODE_ADD_CARRY, I16<>, I16<>, I16<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddCarryXX<ADD_CARRY_I16, Reg16>(e, i);
  }
};
EMITTER(ADD_CARRY_I32, MATCH(I<OPCODE_ADD_CARRY, I32<>, I32<>, I32<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddCarryXX<ADD_CARRY_I32, Reg32>(e, i);
  }
};
EMITTER(ADD_CARRY_I64, MATCH(I<OPCODE_ADD_CARRY, I64<>, I64<>, I64<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddCarryXX<ADD_CARRY_I64, Reg64>(e, i);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_ADD_CARRY,
    ADD_CARRY_I8,
    ADD_CARRY_I16,
    ADD_CARRY_I32,
    ADD_CARRY_I64);


// ============================================================================
// OPCODE_VECTOR_ADD
// ============================================================================
EMITTER(VECTOR_ADD, MATCH(I<OPCODE_VECTOR_ADD, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
        [&i](X64Emitter& e, const Xmm& dest, const Xmm& src1, const Xmm& src2) {
          const TypeName part_type = static_cast<TypeName>(i.instr->flags & 0xFF);
          const uint32_t arithmetic_flags = i.instr->flags >> 8;
          bool is_unsigned = !!(arithmetic_flags & ARITHMETIC_UNSIGNED);
          bool saturate = !!(arithmetic_flags & ARITHMETIC_SATURATE);
          switch (part_type) {
          case INT8_TYPE:
            if (saturate) {
              // TODO(benvanik): trace DID_SATURATE
              if (is_unsigned) {
                e.vpaddusb(dest, src1, src2);
              } else {
                e.vpaddsb(dest, src1, src2);
              }
            } else {
              e.vpaddb(dest, src1, src2);
            }
            break;
          case INT16_TYPE:
            if (saturate) {
              // TODO(benvanik): trace DID_SATURATE
              if (is_unsigned) {
                e.vpaddusw(dest, src1, src2);
              } else {
                e.vpaddsw(dest, src1, src2);
              }
            } else {
              e.vpaddw(dest, src1, src2);
            }
            break;
          case INT32_TYPE:
            if (saturate) {
              if (is_unsigned) {
                // We reuse all these temps...
                XEASSERT(src1 != e.xmm0 && src1 != e.xmm1 && src1 != e.xmm2);
                XEASSERT(src2 != e.xmm0 && src2 != e.xmm1 && src2 != e.xmm2);
                // Clamp to 0xFFFFFFFF.
                // Wish there was a vpaddusd...
                // | A | B | C | D |
                // |     B |     D |
                e.vpsllq(e.xmm0, src1, 32);
                e.vpsllq(e.xmm1, src2, 32);
                e.vpsrlq(e.xmm0, 32);
                e.vpsrlq(e.xmm1, 32);
                e.vpaddq(e.xmm0, e.xmm1);
                e.vpcmpgtq(e.xmm0, e.GetXmmConstPtr(XMMUnsignedDwordMax));
                e.vpsllq(e.xmm0, 32);
                e.vpsrlq(e.xmm0, 32);
                // |     A |     C |
                e.vpsrlq(e.xmm1, src1, 32);
                e.vpsrlq(e.xmm2, src2, 32);
                e.vpaddq(e.xmm1, e.xmm2);
                e.vpcmpgtq(e.xmm1, e.GetXmmConstPtr(XMMUnsignedDwordMax));
                e.vpsllq(e.xmm1, 32);
                // xmm0 = mask for with saturated dwords == 111...
                e.vpor(e.xmm0, e.xmm1);
                e.vpaddd(dest, src1, src2);
                // dest.f[n] = xmm1.f[n] ? xmm1.f[n] : dest.f[n];
                e.vblendvps(dest, dest, e.xmm1, e.xmm1);
              } else {
                XEASSERTALWAYS();
              }
            } else {
              e.vpaddd(dest, src1, src2);
            }
            break;
          case FLOAT32_TYPE:
            e.vaddps(dest, src1, src2);
            break;
          default: XEASSERTALWAYS(); break;
          }
        });
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_VECTOR_ADD,
    VECTOR_ADD);


// ============================================================================
// OPCODE_SUB
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitSubXX(X64Emitter& e, const ARGS& i) {
  if (i.instr->flags & ARITHMETIC_SET_CARRY) {
    // TODO(benvanik): faster way of doing sub with CF set?
    SEQ::EmitAssociativeBinaryOp(
        e, i,
        [](X64Emitter& e, const REG& dest_src, const REG& src) {
          auto temp = GetTempReg<REG>(e);
          e.mov(temp, src);
          e.not(temp);
          e.stc();
          e.adc(dest_src, temp);
        },
        [](X64Emitter& e, const REG& dest_src, int32_t constant) {
          auto temp = GetTempReg<REG>(e);
          e.mov(temp, constant);
          e.not(temp);
          e.stc();
          e.adc(dest_src, temp);
        });
    e.StoreEflags();
  } else {
    SEQ::EmitAssociativeBinaryOp(
        e, i,
        [](X64Emitter& e, const REG& dest_src, const REG& src) { e.sub(dest_src, src); },
        [](X64Emitter& e, const REG& dest_src, int32_t constant) { e.sub(dest_src, constant); });
  }
}
EMITTER(SUB_I8, MATCH(I<OPCODE_SUB, I8<>, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSubXX<SUB_I8, Reg8>(e, i);
  }
};
EMITTER(SUB_I16, MATCH(I<OPCODE_SUB, I16<>, I16<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSubXX<SUB_I16, Reg16>(e, i);
  }
};
EMITTER(SUB_I32, MATCH(I<OPCODE_SUB, I32<>, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSubXX<SUB_I32, Reg32>(e, i);
  }
};
EMITTER(SUB_I64, MATCH(I<OPCODE_SUB, I64<>, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSubXX<SUB_I64, Reg64>(e, i);
  }
};
EMITTER(SUB_F32, MATCH(I<OPCODE_SUB, F32<>, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERT(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vsubss(dest, src1, src2);
        });
  }
};
EMITTER(SUB_F64, MATCH(I<OPCODE_SUB, F64<>, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERT(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vsubsd(dest, src1, src2);
        });
  }
};
EMITTER(SUB_V128, MATCH(I<OPCODE_SUB, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERT(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vsubps(dest, src1, src2);
        });
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_SUB,
    SUB_I8,
    SUB_I16,
    SUB_I32,
    SUB_I64,
    SUB_F32,
    SUB_F64,
    SUB_V128);


// ============================================================================
// OPCODE_MUL
// ============================================================================
// Sign doesn't matter here, as we don't use the high bits.
// We exploit mulx here to avoid creating too much register pressure.
EMITTER(MUL_I8, MATCH(I<OPCODE_MUL, I8<>, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // dest hi, dest low = src * edx
    // TODO(benvanik): place src2 in edx?
    if (i.src1.is_constant) {
      XEASSERT(!i.src2.is_constant);
      e.movzx(e.edx, i.src2);
      e.mov(e.eax, static_cast<uint8_t>(i.src1.constant()));
      e.mulx(e.edx, i.dest.reg().cvt32(), e.eax);
    } else if (i.src2.is_constant) {
      e.movzx(e.edx, i.src1);
      e.mov(e.eax, static_cast<uint8_t>(i.src2.constant()));
      e.mulx(e.edx, i.dest.reg().cvt32(), e.eax);
    } else {
      e.movzx(e.edx, i.src2);
      e.mulx(e.edx, i.dest.reg().cvt32(), i.src1.reg().cvt32());
    }
  }
};
EMITTER(MUL_I16, MATCH(I<OPCODE_MUL, I16<>, I16<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // dest hi, dest low = src * edx
    // TODO(benvanik): place src2 in edx?
    if (i.src1.is_constant) {
      XEASSERT(!i.src2.is_constant);
      e.movzx(e.edx, i.src2);
      e.mov(e.ax, static_cast<uint16_t>(i.src1.constant()));
      e.mulx(e.edx, i.dest.reg().cvt32(), e.eax);
    } else if (i.src2.is_constant) {
      e.movzx(e.edx, i.src1);
      e.mov(e.ax, static_cast<uint16_t>(i.src2.constant()));
      e.mulx(e.edx, i.dest.reg().cvt32(), e.eax);
    } else {
      e.movzx(e.edx, i.src2);
      e.mulx(e.edx, i.dest.reg().cvt32(), i.src1.reg().cvt32());
    }
    e.ReloadEDX();
  }
};
EMITTER(MUL_I32, MATCH(I<OPCODE_MUL, I32<>, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // dest hi, dest low = src * edx
    // TODO(benvanik): place src2 in edx?
    if (i.src1.is_constant) {
      XEASSERT(!i.src2.is_constant);
      e.mov(e.edx, i.src2);
      e.mov(e.eax, i.src1.constant());
      e.mulx(e.edx, i.dest, e.eax);
    } else if (i.src2.is_constant) {
      e.mov(e.edx, i.src1);
      e.mov(e.eax, i.src2.constant());
      e.mulx(e.edx, i.dest, e.eax);
    } else {
      e.mov(e.edx, i.src2);
      e.mulx(e.edx, i.dest, i.src1);
    }
    e.ReloadEDX();
  }
};
EMITTER(MUL_I64, MATCH(I<OPCODE_MUL, I64<>, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // dest hi, dest low = src * rdx
    // TODO(benvanik): place src2 in edx?
    if (i.src1.is_constant) {
      XEASSERT(!i.src2.is_constant);
      e.mov(e.rdx, i.src2);
      e.mov(e.rax, i.src1.constant());
      e.mulx(e.rdx, i.dest, e.rax);
    } else if (i.src2.is_constant) {
      e.mov(e.rdx, i.src1);
      e.mov(e.rax, i.src2.constant());
      e.mulx(e.rdx, i.dest, e.rax);
    } else {
      e.mov(e.rdx, i.src2);
      e.mulx(e.rdx, i.dest, i.src1);
    }
    e.ReloadEDX();
  }
};
EMITTER(MUL_F32, MATCH(I<OPCODE_MUL, F32<>, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERT(!i.instr->flags);
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vmulss(dest, src1, src2);
        });
  }
};
EMITTER(MUL_F64, MATCH(I<OPCODE_MUL, F64<>, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERT(!i.instr->flags);
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vmulsd(dest, src1, src2);
        });
  }
};
EMITTER(MUL_V128, MATCH(I<OPCODE_MUL, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERT(!i.instr->flags);
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vmulps(dest, src1, src2);
        });
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_MUL,
    MUL_I8,
    MUL_I16,
    MUL_I32,
    MUL_I64,
    MUL_F32,
    MUL_F64,
    MUL_V128);


// ============================================================================
// OPCODE_MUL_HI
// ============================================================================
EMITTER(MUL_HI_I8, MATCH(I<OPCODE_MUL_HI, I8<>, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      // TODO(benvanik): place src1 in eax? still need to sign extend
      e.movzx(e.edx, i.src1);
      e.mulx(i.dest.reg().cvt32(), e.eax, i.src2.reg().cvt32());
    } else {
      e.mov(e.al, i.src1);
      e.imul(i.src2);
      e.mov(i.dest, e.ah);
    }
    e.ReloadEDX();
  }
};
EMITTER(MUL_HI_I16, MATCH(I<OPCODE_MUL_HI, I16<>, I16<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      // TODO(benvanik): place src1 in eax? still need to sign extend
      e.movzx(e.edx, i.src1);
      e.mulx(i.dest.reg().cvt32(), e.eax, i.src2.reg().cvt32());
    } else {
      e.mov(e.ax, i.src1);
      e.imul(i.src2);
      e.mov(i.dest, e.dx);
    }
    e.ReloadEDX();
  }
};
EMITTER(MUL_HI_I32, MATCH(I<OPCODE_MUL_HI, I32<>, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      // TODO(benvanik): place src1 in eax? still need to sign extend
      e.mov(e.edx, i.src1);
      if (i.src2.is_constant) {
        e.mov(e.eax, i.src2.constant());
        e.mulx(i.dest, e.edx, e.eax);
      } else {
        e.mulx(i.dest, e.edx, i.src2);
      }
    } else {
      e.mov(e.eax, i.src1);
      e.imul(i.src2);
      e.mov(i.dest, e.edx);
    }
    e.ReloadEDX();
  }
};
EMITTER(MUL_HI_I64, MATCH(I<OPCODE_MUL_HI, I64<>, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      // TODO(benvanik): place src1 in eax? still need to sign extend
      e.mov(e.rdx, i.src1);
      if (i.src2.is_constant) {
        e.mov(e.rax, i.src2.constant());
        e.mulx(i.dest, e.rdx, e.rax);
      } else {
        e.mulx(i.dest, e.rax, i.src2);
      }
    } else {
      e.mov(e.rax, i.src1);
      e.imul(i.src2);
      e.mov(i.dest, e.rdx);
    }
    e.ReloadEDX();
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_MUL_HI,
    MUL_HI_I8,
    MUL_HI_I16,
    MUL_HI_I32,
    MUL_HI_I64);


// ============================================================================
// OPCODE_DIV
// ============================================================================
// TODO(benvanik): optimize common constant cases.
// TODO(benvanik): simplify code!
EMITTER(DIV_I8, MATCH(I<OPCODE_DIV, I8<>, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // NOTE: RDX clobbered.
    bool clobbered_rcx = false;
    if (i.src2.is_constant) {
      XEASSERT(!i.src1.is_constant);
      clobbered_rcx = true;
      e.mov(e.cl, i.src2.constant());
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        e.movzx(e.ax, i.src1);
        e.div(e.cl);
      } else {
        e.movsx(e.ax, i.src1);
        e.idiv(e.cl);
      }
    } else {
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        if (i.src1.is_constant) {
          e.mov(e.ax, static_cast<int16_t>(i.src1.constant()));
        } else {
          e.movzx(e.ax, i.src1);
        }
        e.div(i.src2);
      } else {
        if (i.src1.is_constant) {
          e.mov(e.ax, static_cast<int16_t>(i.src1.constant()));
        } else {
          e.movsx(e.ax, i.src1);
        }
        e.idiv(i.src2);
      }
    }
    e.mov(i.dest, e.al);
    if (clobbered_rcx) {
      e.ReloadECX();
    }
    e.ReloadEDX();
  }
};
EMITTER(DIV_I16, MATCH(I<OPCODE_DIV, I16<>, I16<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // NOTE: RDX clobbered.
    bool clobbered_rcx = false;
    if (i.src2.is_constant) {
      XEASSERT(!i.src1.is_constant);
      clobbered_rcx = true;
      e.mov(e.cx, i.src2.constant());
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        e.mov(e.ax, i.src1);
        // Zero upper bits.
        e.xor(e.dx, e.dx);
        e.div(e.cx);
      } else {
        e.mov(e.ax, i.src1);
        // Set dx to sign bit of src1 (dx:ax = dx:ax / src).
        e.mov(e.dx, e.ax);
        e.sar(e.dx, 15);
        e.idiv(e.cx);
      }
    } else {
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        if (i.src1.is_constant) {
          e.mov(e.ax, i.src1.constant());
        } else {
          e.mov(e.ax, i.src1);
        }
        // Zero upper bits.
        e.xor(e.dx, e.dx);
        e.div(i.src2);
      } else {
        if (i.src1.is_constant) {
          e.mov(e.ax, i.src1.constant());
        } else {
          e.mov(e.ax, i.src1);
        }
        // Set dx to sign bit of src1 (dx:ax = dx:ax / src).
        e.mov(e.dx, e.ax);
        e.sar(e.dx, 15);
        e.idiv(i.src2);
      }
    }
    e.mov(i.dest, e.ax);
    if (clobbered_rcx) {
      e.ReloadECX();
    }
    e.ReloadEDX();
  }
};
EMITTER(DIV_I32, MATCH(I<OPCODE_DIV, I32<>, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // NOTE: RDX clobbered.
    bool clobbered_rcx = false;
    if (i.src2.is_constant) {
      XEASSERT(!i.src1.is_constant);
      clobbered_rcx = true;
      e.mov(e.ecx, i.src2.constant());
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        e.mov(e.eax, i.src1);
        // Zero upper bits.
        e.xor(e.edx, e.edx);
        e.div(e.ecx);
      } else {
        e.mov(e.eax, i.src1);
        // Set dx to sign bit of src1 (dx:ax = dx:ax / src).
        e.mov(e.edx, e.eax);
        e.sar(e.edx, 31);
        e.idiv(e.ecx);
      }
    } else {
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        if (i.src1.is_constant) {
          e.mov(e.eax, i.src1.constant());
        } else {
          e.mov(e.eax, i.src1);
        }
        // Zero upper bits.
        e.xor(e.edx, e.edx);
        e.div(i.src2);
      } else {
        if (i.src1.is_constant) {
          e.mov(e.eax, i.src1.constant());
        } else {
          e.mov(e.eax, i.src1);
        }
        // Set dx to sign bit of src1 (dx:ax = dx:ax / src).
        e.mov(e.edx, e.eax);
        e.sar(e.edx, 31);
        e.idiv(i.src2);
      }
    }
    e.mov(i.dest, e.eax);
    if (clobbered_rcx) {
      e.ReloadECX();
    }
    e.ReloadEDX();
  }
};
EMITTER(DIV_I64, MATCH(I<OPCODE_DIV, I64<>, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // NOTE: RDX clobbered.
    bool clobbered_rcx = false;
    if (i.src2.is_constant) {
      XEASSERT(!i.src1.is_constant);
      clobbered_rcx = true;
      e.mov(e.rcx, i.src2.constant());
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        e.mov(e.rax, i.src1);
        // Zero upper bits.
        e.xor(e.rdx, e.rdx);
        e.div(e.rcx);
      } else {
        e.mov(e.rax, i.src1);
        // Set dx to sign bit of src1 (dx:ax = dx:ax / src).
        e.mov(e.rdx, e.rax);
        e.sar(e.rdx, 63);
        e.idiv(e.rcx);
      }
    } else {
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        if (i.src1.is_constant) {
          e.mov(e.rax, i.src1.constant());
        } else {
          e.mov(e.rax, i.src1);
        }
        // Zero upper bits.
        e.xor(e.rdx, e.rdx);
        e.div(i.src2);
      } else {
        if (i.src1.is_constant) {
          e.mov(e.rax, i.src1.constant());
        } else {
          e.mov(e.rax, i.src1);
        }
        // Set dx to sign bit of src1 (dx:ax = dx:ax / src).
        e.mov(e.rdx, e.rax);
        e.sar(e.rdx, 63);
        e.idiv(i.src2);
      }
    }
    e.mov(i.dest, e.rax);
    if (clobbered_rcx) {
      e.ReloadECX();
    }
    e.ReloadEDX();
  }
};
EMITTER(DIV_F32, MATCH(I<OPCODE_DIV, F32<>, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERT(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vdivss(dest, src1, src2);
        });
  }
};
EMITTER(DIV_F64, MATCH(I<OPCODE_DIV, F64<>, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERT(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vdivsd(dest, src1, src2);
        });
  }
};
EMITTER(DIV_V128, MATCH(I<OPCODE_DIV, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERT(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vdivps(dest, src1, src2);
        });
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_DIV,
    DIV_I8,
    DIV_I16,
    DIV_I32,
    DIV_I64,
    DIV_F32,
    DIV_F64,
    DIV_V128);


// ============================================================================
// OPCODE_MUL_ADD
// ============================================================================
// d = 1 * 2 + 3
// $0 = $1x$0 + $2
// TODO(benvanik): use other forms (132/213/etc) to avoid register shuffling.
// dest could be src2 or src3 - need to ensure it's not before overwriting dest
// perhaps use other 132/213/etc
EMITTER(MUL_ADD_F32, MATCH(I<OPCODE_MUL_ADD, F32<>, F32<>, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.dest == i.src1) {
      e.vfmadd213ss(i.dest, i.src2, i.src3);
    } else {
      if (i.dest != i.src2 && i.dest != i.src3) {
        e.vmovss(i.dest, i.src1);
        e.vfmadd213ss(i.dest, i.src2, i.src3);
      } else {
        e.vmovss(e.xmm0, i.src1);
        e.vfmadd213ss(e.xmm0, i.src2, i.src3);
        e.vmovss(i.dest, e.xmm0);
      }
    }
  }
};
EMITTER(MUL_ADD_F64, MATCH(I<OPCODE_MUL_ADD, F64<>, F64<>, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.dest == i.src1) {
      e.vfmadd213sd(i.dest, i.src2, i.src3);
    } else {
      if (i.dest != i.src2 && i.dest != i.src3) {
        e.vmovsd(i.dest, i.src1);
        e.vfmadd213sd(i.dest, i.src2, i.src3);
      } else {
        e.vmovsd(e.xmm0, i.src1);
        e.vfmadd213sd(e.xmm0, i.src2, i.src3);
        e.vmovsd(i.dest, e.xmm0);
      }
    }
  }
};
EMITTER(MUL_ADD_V128, MATCH(I<OPCODE_MUL_ADD, V128<>, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.dest == i.src1) {
      e.vfmadd213ps(i.dest, i.src2, i.src3);
    } else {
      if (i.dest != i.src2 && i.dest != i.src3) {
        e.vmovdqa(i.dest, i.src1);
        e.vfmadd213ps(i.dest, i.src2, i.src3);
      } else {
        e.vmovdqa(e.xmm0, i.src1);
        e.vfmadd213ps(e.xmm0, i.src2, i.src3);
        e.vmovdqa(i.dest, e.xmm0);
      }
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_MUL_ADD,
    MUL_ADD_F32,
    MUL_ADD_F64,
    MUL_ADD_V128);


// ============================================================================
// OPCODE_MUL_SUB
// ============================================================================
// d = 1 * 2 - 3
// $0 = $2x$0 - $3
// TODO(benvanik): use other forms (132/213/etc) to avoid register shuffling.
// dest could be src2 or src3 - need to ensure it's not before overwriting dest
// perhaps use other 132/213/etc
EMITTER(MUL_SUB_F32, MATCH(I<OPCODE_MUL_SUB, F32<>, F32<>, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.dest == i.src1) {
      e.vfmsub213ss(i.dest, i.src2, i.src3);
    } else {
      if (i.dest != i.src2 && i.dest != i.src3) {
        e.vmovss(i.dest, i.src1);
        e.vfmsub213ss(i.dest, i.src2, i.src3);
      } else {
        e.vmovss(e.xmm0, i.src1);
        e.vfmsub213ss(e.xmm0, i.src2, i.src3);
        e.vmovss(i.dest, e.xmm0);
      }
    }
  }
};
EMITTER(MUL_SUB_F64, MATCH(I<OPCODE_MUL_SUB, F64<>, F64<>, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.dest == i.src1) {
      e.vfmsub213sd(i.dest, i.src2, i.src3);
    } else {
      if (i.dest != i.src2 && i.dest != i.src3) {
        e.vmovsd(i.dest, i.src1);
        e.vfmsub213sd(i.dest, i.src2, i.src3);
      } else {
        e.vmovsd(e.xmm0, i.src1);
        e.vfmsub213sd(e.xmm0, i.src2, i.src3);
        e.vmovsd(i.dest, e.xmm0);
      }
    }
  }
};
EMITTER(MUL_SUB_V128, MATCH(I<OPCODE_MUL_SUB, V128<>, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.dest == i.src1) {
      e.vfmsub213ps(i.dest, i.src2, i.src3);
    } else {
      if (i.dest != i.src2 && i.dest != i.src3) {
        e.vmovdqa(i.dest, i.src1);
        e.vfmsub213ps(i.dest, i.src2, i.src3);
      } else {
        e.vmovdqa(e.xmm0, i.src1);
        e.vfmsub213ps(e.xmm0, i.src2, i.src3);
        e.vmovdqa(i.dest, e.xmm0);
      }
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_MUL_SUB,
    MUL_SUB_F32,
    MUL_SUB_F64,
    MUL_SUB_V128);


// ============================================================================
// OPCODE_NEG
// ============================================================================
// TODO(benvanik): put dest/src1 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitNegXX(X64Emitter& e, const ARGS& i) {
    SEQ::EmitUnaryOp(
        e, i,
        [](X64Emitter& e, const REG& dest_src) { e.neg(dest_src); });
}
EMITTER(NEG_I8, MATCH(I<OPCODE_NEG, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNegXX<NEG_I8, Reg8>(e, i);
  }
};
EMITTER(NEG_I16, MATCH(I<OPCODE_NEG, I16<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNegXX<NEG_I16, Reg16>(e, i);
  }
};
EMITTER(NEG_I32, MATCH(I<OPCODE_NEG, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNegXX<NEG_I32, Reg32>(e, i);
  }
};
EMITTER(NEG_I64, MATCH(I<OPCODE_NEG, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNegXX<NEG_I64, Reg64>(e, i);
  }
};
EMITTER(NEG_F32, MATCH(I<OPCODE_NEG, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vxorps(i.dest, i.src1, e.GetXmmConstPtr(XMMSignMaskPS));
  }
};
EMITTER(NEG_F64, MATCH(I<OPCODE_NEG, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vxorpd(i.dest, i.src1, e.GetXmmConstPtr(XMMSignMaskPD));
  }
};
EMITTER(NEG_V128, MATCH(I<OPCODE_NEG, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERT(!i.instr->flags);
    e.vxorps(i.dest, i.src1, e.GetXmmConstPtr(XMMSignMaskPS));
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_NEG,
    NEG_I8,
    NEG_I16,
    NEG_I32,
    NEG_I64,
    NEG_F32,
    NEG_F64,
    NEG_V128);


// ============================================================================
// OPCODE_ABS
// ============================================================================
EMITTER(ABS_F32, MATCH(I<OPCODE_ABS, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vpand(i.dest, i.src1, e.GetXmmConstPtr(XMMAbsMaskPS));
  }
};
EMITTER(ABS_F64, MATCH(I<OPCODE_ABS, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vpand(i.dest, i.src1, e.GetXmmConstPtr(XMMAbsMaskPD));
  }
};
EMITTER(ABS_V128, MATCH(I<OPCODE_ABS, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vpand(i.dest, i.src1, e.GetXmmConstPtr(XMMAbsMaskPS));
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_ABS,
    ABS_F32,
    ABS_F64,
    ABS_V128);


// ============================================================================
// OPCODE_SQRT
// ============================================================================
EMITTER(SQRT_F32, MATCH(I<OPCODE_SQRT, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vsqrtss(i.dest, i.src1);
  }
};
EMITTER(SQRT_F64, MATCH(I<OPCODE_SQRT, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vsqrtsd(i.dest, i.src1);
  }
};
EMITTER(SQRT_V128, MATCH(I<OPCODE_SQRT, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vsqrtps(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_SQRT,
    SQRT_F32,
    SQRT_F64,
    SQRT_V128);


// ============================================================================
// OPCODE_RSQRT
// ============================================================================
EMITTER(RSQRT_F32, MATCH(I<OPCODE_RSQRT, F32<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vrsqrtss(i.dest, i.src1);
  }
};
EMITTER(RSQRT_F64, MATCH(I<OPCODE_RSQRT, F64<>, F64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vcvtsd2ss(i.dest, i.src1);
    e.vrsqrtss(i.dest, i.dest);
    e.vcvtss2sd(i.dest, i.dest);
  }
};
EMITTER(RSQRT_V128, MATCH(I<OPCODE_RSQRT, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vrsqrtps(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_RSQRT,
    RSQRT_F32,
    RSQRT_F64,
    RSQRT_V128);


// ============================================================================
// OPCODE_POW2
// ============================================================================
// TODO(benvanik): use approx here:
//     http://jrfonseca.blogspot.com/2008/09/fast-sse2-pow-tables-or-polynomials.html
EMITTER(POW2_F32, MATCH(I<OPCODE_POW2, F32<>, F32<>>)) {
  static __m128 EmulatePow2(__m128 src) {
    float result = static_cast<float>(pow(2, src.m128_f32[0]));
    return _mm_load_ss(&result);
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERTALWAYS();
    e.lea(e.r8, e.StashXmm(i.src1));
    e.CallNative(EmulatePow2);
    e.vmovaps(i.dest, e.xmm0);
  }
};
EMITTER(POW2_F64, MATCH(I<OPCODE_POW2, F64<>, F64<>>)) {
  static __m128d EmulatePow2(__m128 src) {
    double result = pow(2, src.m128_f32[0]);
    return _mm_load_sd(&result);
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERTALWAYS();
    e.lea(e.r8, e.StashXmm(i.src1));
    e.CallNative(EmulatePow2);
    e.vmovaps(i.dest, e.xmm0);
  }
};
EMITTER(POW2_V128, MATCH(I<OPCODE_POW2, V128<>, V128<>>)) {
  static __m128 EmulatePow2(__m128 src) {
    __m128 result;
    for (size_t i = 0; i < 4; ++i) {
      result.m128_f32[i] = static_cast<float>(pow(2, src.m128_f32[i]));
    }
    return result;
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.lea(e.r8, e.StashXmm(i.src1));
    e.CallNative(EmulatePow2);
    e.vmovaps(i.dest, e.xmm0);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_POW2,
    POW2_F32,
    POW2_F64,
    POW2_V128);


// ============================================================================
// OPCODE_LOG2
// ============================================================================
// TODO(benvanik): use approx here:
//     http://jrfonseca.blogspot.com/2008/09/fast-sse2-pow-tables-or-polynomials.html
EMITTER(LOG2_F32, MATCH(I<OPCODE_LOG2, F32<>, F32<>>)) {
  static __m128 EmulateLog2(__m128 src) {
    float result = log2(src.m128_f32[0]);
    return _mm_load_ss(&result);
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERTALWAYS();
    e.lea(e.r8, e.StashXmm(i.src1));
    e.CallNative(EmulateLog2);
    e.vmovaps(i.dest, e.xmm0);
  }
};
EMITTER(LOG2_F64, MATCH(I<OPCODE_LOG2, F64<>, F64<>>)) {
  static __m128d EmulateLog2(__m128d src) {
    double result = log2(src.m128d_f64[0]);
    return _mm_load_sd(&result);
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERTALWAYS();
    e.lea(e.r8, e.StashXmm(i.src1));
    e.CallNative(EmulateLog2);
    e.vmovaps(i.dest, e.xmm0);
  }
};
EMITTER(LOG2_V128, MATCH(I<OPCODE_LOG2, V128<>, V128<>>)) {
  static __m128 EmulateLog2(__m128 src) {
    __m128 result;
    for (size_t i = 0; i < 4; ++i) {
      result.m128_f32[i] = log2(src.m128_f32[i]);
    }
    return result;
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    XEASSERTALWAYS();
    e.lea(e.r8, e.StashXmm(i.src1));
    e.CallNative(EmulateLog2);
    e.vmovaps(i.dest, e.xmm0);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_LOG2,
    LOG2_F32,
    LOG2_F64,
    LOG2_V128);


// ============================================================================
// OPCODE_DOT_PRODUCT_3
// ============================================================================
EMITTER(DOT_PRODUCT_3_V128, MATCH(I<OPCODE_DOT_PRODUCT_3, F32<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // http://msdn.microsoft.com/en-us/library/bb514054(v=vs.90).aspx
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          // TODO(benvanik): apparently this is very slow - find alternative?
          e.vdpps(dest, src1, src2, B01110001);
        });
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_DOT_PRODUCT_3,
    DOT_PRODUCT_3_V128);


// ============================================================================
// OPCODE_DOT_PRODUCT_4
// ============================================================================
EMITTER(DOT_PRODUCT_4_V128, MATCH(I<OPCODE_DOT_PRODUCT_4, F32<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // http://msdn.microsoft.com/en-us/library/bb514054(v=vs.90).aspx
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          // TODO(benvanik): apparently this is very slow - find alternative?
          e.vdpps(dest, src1, src2, B11110001);
        });
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_DOT_PRODUCT_4,
    DOT_PRODUCT_4_V128);


// ============================================================================
// OPCODE_AND
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitAndXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitCommutativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const REG& src) { e.and(dest_src, src); },
      [](X64Emitter& e, const REG& dest_src, int32_t constant) { e.and(dest_src, constant); });
}
EMITTER(AND_I8, MATCH(I<OPCODE_AND, I8<>, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAndXX<AND_I8, Reg8>(e, i);
  }
};
EMITTER(AND_I16, MATCH(I<OPCODE_AND, I16<>, I16<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAndXX<AND_I16, Reg16>(e, i);
  }
};
EMITTER(AND_I32, MATCH(I<OPCODE_AND, I32<>, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAndXX<AND_I32, Reg32>(e, i);
  }
};
EMITTER(AND_I64, MATCH(I<OPCODE_AND, I64<>, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAndXX<AND_I64, Reg64>(e, i);
  }
};
EMITTER(AND_V128, MATCH(I<OPCODE_AND, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vpand(dest, src1, src2);
        });
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_AND,
    AND_I8,
    AND_I16,
    AND_I32,
    AND_I64,
    AND_V128);


// ============================================================================
// OPCODE_OR
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitOrXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitCommutativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const REG& src) { e.or(dest_src, src); },
      [](X64Emitter& e, const REG& dest_src, int32_t constant) { e.or(dest_src, constant); });
}
EMITTER(OR_I8, MATCH(I<OPCODE_OR, I8<>, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitOrXX<OR_I8, Reg8>(e, i);
  }
};
EMITTER(OR_I16, MATCH(I<OPCODE_OR, I16<>, I16<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitOrXX<OR_I16, Reg16>(e, i);
  }
};
EMITTER(OR_I32, MATCH(I<OPCODE_OR, I32<>, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitOrXX<OR_I32, Reg32>(e, i);
  }
};
EMITTER(OR_I64, MATCH(I<OPCODE_OR, I64<>, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitOrXX<OR_I64, Reg64>(e, i);
  }
};
EMITTER(OR_V128, MATCH(I<OPCODE_OR, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vpor(dest, src1, src2);
        });
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_OR,
    OR_I8,
    OR_I16,
    OR_I32,
    OR_I64,
    OR_V128);


// ============================================================================
// OPCODE_XOR
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitXorXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitCommutativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const REG& src) { e.xor(dest_src, src); },
      [](X64Emitter& e, const REG& dest_src, int32_t constant) { e.xor(dest_src, constant); });
}
EMITTER(XOR_I8, MATCH(I<OPCODE_XOR, I8<>, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitXorXX<XOR_I8, Reg8>(e, i);
  }
};
EMITTER(XOR_I16, MATCH(I<OPCODE_XOR, I16<>, I16<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitXorXX<XOR_I16, Reg16>(e, i);
  }
};
EMITTER(XOR_I32, MATCH(I<OPCODE_XOR, I32<>, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitXorXX<XOR_I32, Reg32>(e, i);
  }
};
EMITTER(XOR_I64, MATCH(I<OPCODE_XOR, I64<>, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitXorXX<XOR_I64, Reg64>(e, i);
  }
};
EMITTER(XOR_V128, MATCH(I<OPCODE_XOR, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
        [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          e.vpxor(dest, src1, src2);
        });
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_XOR,
    XOR_I8,
    XOR_I16,
    XOR_I32,
    XOR_I64,
    XOR_V128);


// ============================================================================
// OPCODE_NOT
// ============================================================================
// TODO(benvanik): put dest/src1 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitNotXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitUnaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src) { e.not(dest_src); });
}
EMITTER(NOT_I8, MATCH(I<OPCODE_NOT, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNotXX<NOT_I8, Reg8>(e, i);
  }
};
EMITTER(NOT_I16, MATCH(I<OPCODE_NOT, I16<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNotXX<NOT_I16, Reg16>(e, i);
  }
};
EMITTER(NOT_I32, MATCH(I<OPCODE_NOT, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNotXX<NOT_I32, Reg32>(e, i);
  }
};
EMITTER(NOT_I64, MATCH(I<OPCODE_NOT, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNotXX<NOT_I64, Reg64>(e, i);
  }
};
EMITTER(NOT_V128, MATCH(I<OPCODE_NOT, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // dest = src ^ 0xFFFF...
    e.vpxor(i.dest, i.src1, e.GetXmmConstPtr(XMMOne));
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_NOT,
    NOT_I8,
    NOT_I16,
    NOT_I32,
    NOT_I64,
    NOT_V128);


// ============================================================================
// OPCODE_SHL
// ============================================================================
// TODO(benvanik): optimize common shifts.
template <typename SEQ, typename REG, typename ARGS>
void EmitShlXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitAssociativeBinaryOp(
        e, i,
        [](X64Emitter& e, const REG& dest_src, const Reg8& src) {
          if (dest_src.getBit() == 64) {
            e.shlx(dest_src.cvt64(), dest_src.cvt64(), src.cvt64());
          } else {
            e.shlx(dest_src.cvt32(), dest_src.cvt32(), src.cvt32());
          }
        }, [](X64Emitter& e, const REG& dest_src, int8_t constant) {
          e.shl(dest_src, constant);
        });
}
EMITTER(SHL_I8, MATCH(I<OPCODE_SHL, I8<>, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShlXX<SHL_I8, Reg8>(e, i);
  }
};
EMITTER(SHL_I16, MATCH(I<OPCODE_SHL, I16<>, I16<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShlXX<SHL_I16, Reg16>(e, i);
  }
};
EMITTER(SHL_I32, MATCH(I<OPCODE_SHL, I32<>, I32<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShlXX<SHL_I32, Reg32>(e, i);
  }
};
EMITTER(SHL_I64, MATCH(I<OPCODE_SHL, I64<>, I64<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShlXX<SHL_I64, Reg64>(e, i);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_SHL,
    SHL_I8,
    SHL_I16,
    SHL_I32,
    SHL_I64);


// ============================================================================
// OPCODE_SHR
// ============================================================================
// TODO(benvanik): optimize common shifts.
template <typename SEQ, typename REG, typename ARGS>
void EmitShrXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitAssociativeBinaryOp(
        e, i,
        [](X64Emitter& e, const REG& dest_src, const Reg8& src) {
          if (dest_src.getBit() == 64) {
            e.shrx(dest_src.cvt64(), dest_src.cvt64(), src.cvt64());
          } else if (dest_src.getBit() == 32) {
            e.shrx(dest_src.cvt32(), dest_src.cvt32(), src.cvt32());
          } else {
            e.movzx(dest_src.cvt32(), dest_src);
            e.shrx(dest_src.cvt32(), dest_src.cvt32(), src.cvt32());
          }
        }, [](X64Emitter& e, const REG& dest_src, int8_t constant) {
          e.shr(dest_src, constant);
        });
}
EMITTER(SHR_I8, MATCH(I<OPCODE_SHR, I8<>, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShrXX<SHR_I8, Reg8>(e, i);
  }
};
EMITTER(SHR_I16, MATCH(I<OPCODE_SHR, I16<>, I16<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShrXX<SHR_I16, Reg16>(e, i);
  }
};
EMITTER(SHR_I32, MATCH(I<OPCODE_SHR, I32<>, I32<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShrXX<SHR_I32, Reg32>(e, i);
  }
};
EMITTER(SHR_I64, MATCH(I<OPCODE_SHR, I64<>, I64<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShrXX<SHR_I64, Reg64>(e, i);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_SHR,
    SHR_I8,
    SHR_I16,
    SHR_I32,
    SHR_I64);


// ============================================================================
// OPCODE_SHA
// ============================================================================
// TODO(benvanik): optimize common shifts.
template <typename SEQ, typename REG, typename ARGS>
void EmitSarXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitAssociativeBinaryOp(
        e, i,
        [](X64Emitter& e, const REG& dest_src, const Reg8& src) {
          if (dest_src.getBit() == 64) {
            e.sarx(dest_src.cvt64(), dest_src.cvt64(), src.cvt64());
          } else if (dest_src.getBit() == 32) {
            e.sarx(dest_src.cvt32(), dest_src.cvt32(), src.cvt32());
          } else {
            e.movsx(dest_src.cvt32(), dest_src);
            e.sarx(dest_src.cvt32(), dest_src.cvt32(), src.cvt32());
          }
        }, [](X64Emitter& e, const REG& dest_src, int8_t constant) {
          e.sar(dest_src, constant);
        });
}
EMITTER(SHA_I8, MATCH(I<OPCODE_SHA, I8<>, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSarXX<SHA_I8, Reg8>(e, i);
  }
};
EMITTER(SHA_I16, MATCH(I<OPCODE_SHA, I16<>, I16<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSarXX<SHA_I16, Reg16>(e, i);
  }
};
EMITTER(SHA_I32, MATCH(I<OPCODE_SHA, I32<>, I32<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSarXX<SHA_I32, Reg32>(e, i);
  }
};
EMITTER(SHA_I64, MATCH(I<OPCODE_SHA, I64<>, I64<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSarXX<SHA_I64, Reg64>(e, i);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_SHA,
    SHA_I8,
    SHA_I16,
    SHA_I32,
    SHA_I64);


// ============================================================================
// OPCODE_VECTOR_SHL
// ============================================================================
EMITTER(VECTOR_SHL_V128, MATCH(I<OPCODE_VECTOR_SHL, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
    case INT8_TYPE:
      EmitInt8(e, i);
      break;
    case INT16_TYPE:
      EmitInt16(e, i);
      break;
    case INT32_TYPE:
      EmitInt32(e, i);
      break;
    default:
      XEASSERTALWAYS();
      break;
    }
  }
  static void EmitInt8(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 16 - n; ++n) {
        if (shamt.b16[n] != shamt.b16[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same.
        uint8_t sh = shamt.b16[0] & 0x7;
        if (!sh) {
          // No shift?
          e.vmovaps(i.dest, i.src1);
        } else {
          // Even bytes.
          e.vpsrlw(e.xmm0, i.src1, 8);
          e.vpsllw(e.xmm0, sh + 8);
          // Odd bytes.
          e.vpsllw(i.dest, i.src1, 8);
          e.vpsrlw(i.dest, 8 - sh);
          // Mix.
          e.vpor(i.dest, e.xmm0);
        }
      } else {
        // Counts differ, so pre-mask and load constant.
        XEASSERTALWAYS();
      }
    } else {
      // Fully variable shift.
      // TODO(benvanik): find a better sequence.
      Xmm temp = i.dest;
      if (i.dest == i.src1 || i.dest == i.src2) {
        temp = e.xmm2;
      }
      auto byte_mask = e.GetXmmConstPtr(XMMShiftByteMask);
      // AABBCCDD|EEFFGGHH|IIJJKKLL|MMNNOOPP
      //       DD|      HH|      LL|      PP
      e.vpand(e.xmm0, i.src1, byte_mask);
      e.vpand(e.xmm1, i.src2, byte_mask);
      e.vpsllvd(temp, e.xmm0, e.xmm1);
      //     CC  |    GG  |    KK  |    OO
      e.vpsrld(e.xmm0, i.src1, 8);
      e.vpand(e.xmm0, byte_mask);
      e.vpsrld(e.xmm1, i.src2, 8);
      e.vpand(e.xmm1, byte_mask);
      e.vpsllvd(e.xmm0, e.xmm0, e.xmm1);
      e.vpslld(e.xmm0, 8);
      e.vpor(temp, e.xmm0);
      //   BB    |  FF    |  JJ    |  NN
      e.vpsrld(e.xmm0, i.src1, 16);
      e.vpand(e.xmm0, byte_mask);
      e.vpsrld(e.xmm1, i.src2, 16);
      e.vpand(e.xmm1, byte_mask);
      e.vpsllvd(e.xmm0, e.xmm0, e.xmm1);
      e.vpslld(e.xmm0, 16);
      e.vpor(temp, e.xmm0);
      // AA      |EE      |II      |MM
      e.vpsrld(e.xmm0, i.src1, 24);
      e.vpand(e.xmm0, byte_mask);
      e.vpsrld(e.xmm1, i.src2, 24);
      e.vpand(e.xmm1, byte_mask);
      e.vpsllvd(e.xmm0, e.xmm0, e.xmm1);
      e.vpslld(e.xmm0, 24);
      e.vpor(i.dest, temp, e.xmm0);
    }
  }
  static void EmitInt16(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 8 - n; ++n) {
        if (shamt.s8[n] != shamt.s8[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same, so we can use vpsllw.
        e.vpsllw(i.dest, i.src1, shamt.s8[0] & 0xF);
      } else {
        // Counts differ, so pre-mask and load constant.
        XEASSERTALWAYS();
      }
    } else {
      // Fully variable shift.
      XEASSERTALWAYS();
    }
  }
  static void EmitInt32(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 4 - n; ++n) {
        if (shamt.i4[n] != shamt.i4[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same, so we can use vpslld.
        e.vpslld(i.dest, i.src1, shamt.b16[0] & 0x1F);
      } else {
        // Counts differ, so pre-mask and load constant.
        vec128_t masked = i.src2.constant();
        for (size_t n = 0; n < 4; ++n) {
          masked.i4[n] &= 0x1F;
        }
        e.LoadConstantXmm(e.xmm0, masked);
        e.vpsllvd(i.dest, i.src1, e.xmm0);
      }
    } else {
      // Fully variable shift.
      // src shift mask may have values >31, and x86 sets to zero when
      // that happens so we mask.
      e.vandps(e.xmm0, i.src2, e.GetXmmConstPtr(XMMShiftMaskPS));
      e.vpsllvd(i.dest, i.src1, e.xmm0);
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_VECTOR_SHL,
    VECTOR_SHL_V128);


// ============================================================================
// OPCODE_VECTOR_SHR
// ============================================================================
EMITTER(VECTOR_SHR_V128, MATCH(I<OPCODE_VECTOR_SHR, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
    case INT8_TYPE:
      EmitInt8(e, i);
      break;
    case INT16_TYPE:
      EmitInt16(e, i);
      break;
    case INT32_TYPE:
      EmitInt32(e, i);
      break;
    default:
      XEASSERTALWAYS();
      break;
    }
  }
  static void EmitInt8(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 16 - n; ++n) {
        if (shamt.b16[n] != shamt.b16[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same.
        uint8_t sh = shamt.b16[0] & 0x7;
        if (!sh) {
          // No shift?
          e.vmovaps(i.dest, i.src1);
        } else {
          // Even bytes.
          e.vpsllw(e.xmm0, i.src1, 8);
          e.vpsrlw(e.xmm0, sh + 8);
          // Odd bytes.
          e.vpsrlw(i.dest, i.src1, 8);
          e.vpsllw(i.dest, 8 - sh);
          // Mix.
          e.vpor(i.dest, e.xmm0);
        }
      } else {
        // Counts differ, so pre-mask and load constant.
        XEASSERTALWAYS();
      }
    } else {
      // Fully variable shift.
      XEASSERTALWAYS();
    }
  }
  static void EmitInt16(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 8 - n; ++n) {
        if (shamt.s8[n] != shamt.s8[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same, so we can use vpsllw.
        e.vpsrlw(i.dest, i.src1, shamt.s8[0] & 0xF);
      } else {
        // Counts differ, so pre-mask and load constant.
        XEASSERTALWAYS();
      }
    } else {
      // Fully variable shift.
      XEASSERTALWAYS();
    }
  }
  static void EmitInt32(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 4 - n; ++n) {
        if (shamt.i4[n] != shamt.i4[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same, so we can use vpslld.
        e.vpsrld(i.dest, i.src1, shamt.b16[0] & 0x1F);
      } else {
        // Counts differ, so pre-mask and load constant.
        vec128_t masked = i.src2.constant();
        for (size_t n = 0; n < 4; ++n) {
          masked.i4[n] &= 0x1F;
        }
        e.LoadConstantXmm(e.xmm0, masked);
        e.vpsrlvd(i.dest, i.src1, e.xmm0);
      }
    } else {
      // Fully variable shift.
      // src shift mask may have values >31, and x86 sets to zero when
      // that happens so we mask.
      e.vandps(e.xmm0, i.src2, e.GetXmmConstPtr(XMMShiftMaskPS));
      e.vpsrlvd(i.dest, i.src1, e.xmm0);
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_VECTOR_SHR,
    VECTOR_SHR_V128);


// ============================================================================
// OPCODE_VECTOR_SHA
// ============================================================================
EMITTER(VECTOR_SHA_V128, MATCH(I<OPCODE_VECTOR_SHA, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
    case INT32_TYPE:
      // src shift mask may have values >31, and x86 sets to zero when
      // that happens so we mask.
      e.vandps(e.xmm0, i.src2, e.GetXmmConstPtr(XMMShiftMaskPS));
      e.vpsravd(i.dest, i.src1, e.xmm0);
      break;
    default:
      XEASSERTALWAYS();
      break;
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_VECTOR_SHA,
    VECTOR_SHA_V128);


// ============================================================================
// OPCODE_ROTATE_LEFT
// ============================================================================
// TODO(benvanik): put dest/src1 together, src2 in cl.
template <typename SEQ, typename REG, typename ARGS>
void EmitRotateLeftXX(X64Emitter& e, const ARGS& i) {
  if (i.src2.is_constant) {
    // Constant rotate.
    if (i.dest != i.src1) {
      if (i.src1.is_constant) {
        e.mov(i.dest, i.src1.constant());
      } else {
        e.mov(i.dest, i.src1);
      }
    }
    e.rol(i.dest, i.src2.constant());
  } else {
    // Variable rotate.
    if (i.src2.reg().getIdx() != e.cl.getIdx()) {
      e.mov(e.cl, i.src2);
    }
    if (i.dest != i.src1) {
      if (i.src1.is_constant) {
        e.mov(i.dest, i.src1.constant());
      } else {
        e.mov(i.dest, i.src1);
      }
    }
    e.rol(i.dest, e.cl);
    e.ReloadECX();
  }
}
EMITTER(ROTATE_LEFT_I8, MATCH(I<OPCODE_ROTATE_LEFT, I8<>, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitRotateLeftXX<ROTATE_LEFT_I8, Reg8>(e, i);
  }
};
EMITTER(ROTATE_LEFT_I16, MATCH(I<OPCODE_ROTATE_LEFT, I16<>, I16<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitRotateLeftXX<ROTATE_LEFT_I16, Reg16>(e, i);
  }
};
EMITTER(ROTATE_LEFT_I32, MATCH(I<OPCODE_ROTATE_LEFT, I32<>, I32<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitRotateLeftXX<ROTATE_LEFT_I32, Reg32>(e, i);
  }
};
EMITTER(ROTATE_LEFT_I64, MATCH(I<OPCODE_ROTATE_LEFT, I64<>, I64<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitRotateLeftXX<ROTATE_LEFT_I64, Reg64>(e, i);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_ROTATE_LEFT,
    ROTATE_LEFT_I8,
    ROTATE_LEFT_I16,
    ROTATE_LEFT_I32,
    ROTATE_LEFT_I64);


// ============================================================================
// OPCODE_BYTE_SWAP
// ============================================================================
// TODO(benvanik): put dest/src1 together.
EMITTER(BYTE_SWAP_I16, MATCH(I<OPCODE_BYTE_SWAP, I16<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(
        e, i,
        [](X64Emitter& e, const Reg16& dest_src) { e.ror(dest_src, 8); });
  }
};
EMITTER(BYTE_SWAP_I32, MATCH(I<OPCODE_BYTE_SWAP, I32<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(
        e, i,
        [](X64Emitter& e, const Reg32& dest_src) { e.bswap(dest_src); });
  }
};
EMITTER(BYTE_SWAP_I64, MATCH(I<OPCODE_BYTE_SWAP, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(
        e, i,
        [](X64Emitter& e, const Reg64& dest_src) { e.bswap(dest_src); });
  }
};
EMITTER(BYTE_SWAP_V128, MATCH(I<OPCODE_BYTE_SWAP, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): find a way to do this without the memory load.
    e.vpshufb(i.dest, i.src1, e.GetXmmConstPtr(XMMByteSwapMask));
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_BYTE_SWAP,
    BYTE_SWAP_I16,
    BYTE_SWAP_I32,
    BYTE_SWAP_I64,
    BYTE_SWAP_V128);


// ============================================================================
// OPCODE_CNTLZ
// ============================================================================
EMITTER(CNTLZ_I8, MATCH(I<OPCODE_CNTLZ, I8<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // No 8bit lzcnt, so do 16 and sub 8.
    e.movzx(i.dest.reg().cvt16(), i.src1);
    e.lzcnt(i.dest.reg().cvt16(), i.dest.reg().cvt16());
    e.sub(i.dest, 8);
  }
};
EMITTER(CNTLZ_I16, MATCH(I<OPCODE_CNTLZ, I8<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.lzcnt(i.dest.reg().cvt32(), i.src1);
  }
};
EMITTER(CNTLZ_I32, MATCH(I<OPCODE_CNTLZ, I8<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.lzcnt(i.dest.reg().cvt32(), i.src1);
  }
};
EMITTER(CNTLZ_I64, MATCH(I<OPCODE_CNTLZ, I8<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.lzcnt(i.dest.reg().cvt64(), i.src1);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_CNTLZ,
    CNTLZ_I8,
    CNTLZ_I16,
    CNTLZ_I32,
    CNTLZ_I64);


// ============================================================================
// OPCODE_INSERT
// ============================================================================


// ============================================================================
// OPCODE_EXTRACT
// ============================================================================
// TODO(benvanik): sequence extract/splat:
//  v0.i32 = extract v0.v128, 0
//  v0.v128 = splat v0.i32
// This can be a single broadcast.
EMITTER(EXTRACT_I8, MATCH(I<OPCODE_EXTRACT, I8<>, V128<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.vpextrb(i.dest.reg().cvt32(), i.src1, VEC128_B(i.src2.constant()));
    } else {
      XEASSERTALWAYS();
      // TODO(benvanik): try out hlide's version:
      // e.mov(e.eax, 0x80808003);
      // e.xor(e.al, i.src2);
      // e.and(e.al, 15);
      // e.vmovd(e.xmm0, e.eax);
      // e.vpshufb(e.xmm0, i.src1, e.xmm0);
      // e.vmovd(i.dest.reg().cvt32(), e.xmm0); 
    }
  }
};
EMITTER(EXTRACT_I16, MATCH(I<OPCODE_EXTRACT, I16<>, V128<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.vpextrw(i.dest.reg().cvt32(), i.src1, VEC128_W(i.src2.constant()));
    } else {
      // TODO(benvanik): try out hlide's version:
      // e.mov(e.eax, 7);
      // e.and(e.al, i.src2);        // eax = [i&7, 0, 0, 0]
      // e.imul(e.eax, 0x00000202);   // [(i&7)*2, (i&7)*2, 0, 0]
      // e.xor(e.eax, 0x80800203);    // [((i&7)*2)^3, ((i&7)*2)^2, 0x80, 0x80]
      // e.vmovd(e.xmm0, e.eax);
      // e.vpshufb(e.xmm0, i.src1, e.xmm0);
      // e.vmovd(i.dest.reg().cvt32(), e.xmm0);
      XEASSERTALWAYS();
    }
  }
};
EMITTER(EXTRACT_I32, MATCH(I<OPCODE_EXTRACT, I32<>, V128<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    static vec128_t extract_table_32[4] = {
      vec128b( 3,  2,  1,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0),
      vec128b( 7,  6,  5,  4,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0),
      vec128b(11, 10,  9,  8,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0),
      vec128b(15, 14, 13, 12,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0),
    };
    if (i.src2.is_constant) {
      e.vpextrd(i.dest, i.src1, VEC128_D(i.src2.constant()));
    } else {
      // TODO(benvanik): try out hlide's version:
      // e.mov(e.eax, 3);
      // e.and(e.al, i.src2);       // eax = [(i&3), 0, 0, 0]
      // e.imul(e.eax, 0x04040404); // [(i&3)*4, (i&3)*4, (i&3)*4, (i&3)*4]
      // e.add(e.eax, 0x00010203);  // [((i&3)*4)+3, ((i&3)*4)+2, ((i&3)*4)+1, ((i&3)*4)+0]
      // e.vmovd(e.xmm0, e.eax);
      // e.vpshufb(e.xmm0, i.src1, e.xmm0);
      // e.vmovd(i.dest.reg().cvt32(), e.xmm0);
      // Get the desired word in xmm0, then extract that.
      e.xor(e.rax, e.rax);
      e.mov(e.al, i.src2);
      e.and(e.al, 0x03);
      e.shl(e.al, 4);
      e.mov(e.rdx, reinterpret_cast<uint64_t>(extract_table_32));
      e.vmovaps(e.xmm0, e.ptr[e.rdx + e.rax]);
      e.vpshufb(e.xmm0, i.src1, e.xmm0);
      e.vpextrd(i.dest, e.xmm0, 0);
      e.ReloadEDX();
    }
  }
};
EMITTER(EXTRACT_F32, MATCH(I<OPCODE_EXTRACT, F32<>, V128<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.vextractps(i.dest, i.src1, VEC128_F(i.src2.constant()));
    } else {
      XEASSERTALWAYS();
      // TODO(benvanik): try out hlide's version:
      // e.mov(e.eax, 3);
      // e.and(e.al, i.src2);       // eax = [(i&3), 0, 0, 0]
      // e.imul(e.eax, 0x04040404); // [(i&3)*4, (i&3)*4, (i&3)*4, (i&3)*4]
      // e.add(e.eax, 0x00010203);  // [((i&3)*4)+3, ((i&3)*4)+2, ((i&3)*4)+1, ((i&3)*4)+0]
      // e.vmovd(e.xmm0, e.eax);
      // e.vpshufb(e.xmm0, i.src1, e.xmm0);
      // e.vmovd(i.dest, e.xmm0);
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_EXTRACT,
    EXTRACT_I8,
    EXTRACT_I16,
    EXTRACT_I32,
    EXTRACT_F32);


// ============================================================================
// OPCODE_SPLAT
// ============================================================================
EMITTER(SPLAT_I8, MATCH(I<OPCODE_SPLAT, V128<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      // TODO(benvanik): faster constant splats.
      e.mov(e.al, i.src1.constant());
      e.vmovd(e.xmm0, e.eax);
      e.vpbroadcastb(i.dest, e.xmm0);
    } else {
      e.vmovd(e.xmm0, i.src1.reg().cvt32());
      e.vpbroadcastb(i.dest, e.xmm0);
    }
  }
};
EMITTER(SPLAT_I16, MATCH(I<OPCODE_SPLAT, V128<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      // TODO(benvanik): faster constant splats.
      e.mov(e.ax, i.src1.constant());
      e.vmovd(e.xmm0, e.eax);
      e.vpbroadcastw(i.dest, e.xmm0);
    } else {
      e.vmovd(e.xmm0, i.src1.reg().cvt32());
      e.vpbroadcastw(i.dest, e.xmm0);
    }
  }
};
EMITTER(SPLAT_I32, MATCH(I<OPCODE_SPLAT, V128<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      // TODO(benvanik): faster constant splats.
      e.mov(e.eax, i.src1.constant());
      e.vmovd(e.xmm0, e.eax);
      e.vpbroadcastd(i.dest, e.xmm0);
    } else {
      e.vmovd(e.xmm0, i.src1);
      e.vpbroadcastd(i.dest, e.xmm0);
    }
  }
};
EMITTER(SPLAT_F32, MATCH(I<OPCODE_SPLAT, V128<>, F32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      // TODO(benvanik): faster constant splats.
      e.mov(e.eax, i.src1.value->constant.i32);
      e.vmovd(e.xmm0, e.eax);
      e.vbroadcastss(i.dest, e.xmm0);
    } else {
      e.vbroadcastss(i.dest, i.src1);
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_SPLAT,
    SPLAT_I8,
    SPLAT_I16,
    SPLAT_I32,
    SPLAT_F32);


// ============================================================================
// OPCODE_PERMUTE
// ============================================================================
EMITTER(PERMUTE_I32, MATCH(I<OPCODE_PERMUTE, V128<>, I32<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // Permute words between src2 and src3.
    // TODO(benvanik): check src3 for zero. if 0, we can use pshufb.
    if (i.src1.is_constant) {
      uint32_t control = i.src1.constant();
      // Shuffle things into the right places in dest & xmm0,
      // then we blend them together.
      uint32_t src_control =
          (((control >> 24) & 0x3) << 0) |
          (((control >> 16) & 0x3) << 2) |
          (((control >> 8) & 0x3) << 4) |
          (((control >> 0) & 0x3) << 6);
      uint32_t blend_control =
          (((control >> 26) & 0x1) << 0) |
          (((control >> 18) & 0x1) << 1) |
          (((control >> 10) & 0x1) << 2) |
          (((control >> 2) & 0x1) << 3);
      // TODO(benvanik): if src2/src3 are constants, shuffle now!
      Xmm src2;
      if (i.src2.is_constant) {
        src2 = e.xmm1;
        e.LoadConstantXmm(src2, i.src2.constant());
      } else {
        src2 = i.src2;
      }
      Xmm src3;
      if (i.src3.is_constant) {
        src3 = e.xmm2;
        e.LoadConstantXmm(src3, i.src3.constant());
      } else {
        src3 = i.src3;
      }
      if (i.dest != src3) {
        e.vpshufd(i.dest, src2, src_control);
        e.vpshufd(e.xmm0, src3, src_control);
        e.vpblendd(i.dest, e.xmm0, blend_control);
      } else {
        e.vmovaps(e.xmm0, src3);
        e.vpshufd(i.dest, src2, src_control);
        e.vpshufd(e.xmm0, e.xmm0, src_control);
        e.vpblendd(i.dest, e.xmm0, blend_control);
      }
    } else {
      // Permute by non-constant.
      XEASSERTALWAYS();
    }
  }
};
EMITTER(PERMUTE_V128, MATCH(I<OPCODE_PERMUTE, V128<>, V128<>, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): find out how to do this with only one temp register!
    // Permute bytes between src2 and src3.
    if (i.src3.value->IsConstantZero()) {
      // Permuting with src2/zero, so just shuffle/mask.
      if (i.src2.value->IsConstantZero()) {
        // src2 & src3 are zero, so result will always be zero.
        e.vpxor(i.dest, i.dest);
      } else {
        // Control mask needs to be shuffled.
        e.vpshufb(e.xmm0, i.src1, e.GetXmmConstPtr(XMMByteSwapMask));
        if (i.src2.is_constant) {
          e.LoadConstantXmm(i.dest, i.src2.constant());
          e.vpshufb(i.dest, i.dest, e.xmm0);
        } else {
          e.vpshufb(i.dest, i.src2, e.xmm0);
        }
        // Build a mask with values in src2 having 0 and values in src3 having 1.
        e.vpcmpgtb(e.xmm0, e.xmm0, e.GetXmmConstPtr(XMMPermuteControl15));
        e.vpandn(i.dest, e.xmm0, i.dest);
      }
    } else {
      // General permute.
      // Control mask needs to be shuffled.
      if (i.src1.is_constant) {
        e.LoadConstantXmm(e.xmm2, i.src1.constant());
        e.vpshufb(e.xmm2, e.xmm2, e.GetXmmConstPtr(XMMByteSwapMask));
      } else {
        e.vpshufb(e.xmm2, i.src1, e.GetXmmConstPtr(XMMByteSwapMask));
      }
      Xmm src2_shuf = e.xmm0;
      if (i.src2.value->IsConstantZero()) {
        e.vpxor(src2_shuf, src2_shuf);
      } else if (i.src2.is_constant) {
        e.LoadConstantXmm(src2_shuf, i.src2.constant());
        e.vpshufb(src2_shuf, src2_shuf, e.xmm2);
      } else {
        e.vpshufb(src2_shuf, i.src2, e.xmm2);
      }
      Xmm src3_shuf = e.xmm1;
      if (i.src3.value->IsConstantZero()) {
        e.vpxor(src3_shuf, src3_shuf);
      } else if (i.src3.is_constant) {
        e.LoadConstantXmm(src3_shuf, i.src3.constant());
        e.vpshufb(src3_shuf, src3_shuf, e.xmm2);
      } else {
        e.vpshufb(src3_shuf, i.src3, e.xmm2);
      }
      // Build a mask with values in src2 having 0 and values in src3 having 1.
      e.vpcmpgtb(i.dest, e.xmm2, e.GetXmmConstPtr(XMMPermuteControl15));
      e.vpblendvb(i.dest, src2_shuf, src3_shuf, i.dest);
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_PERMUTE,
    PERMUTE_I32,
    PERMUTE_V128);


// ============================================================================
// OPCODE_SWIZZLE
// ============================================================================
EMITTER(SWIZZLE, MATCH(I<OPCODE_SWIZZLE, V128<>, V128<>, OffsetOp>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto element_type = i.instr->flags;
    if (element_type == INT8_TYPE) {
      XEASSERTALWAYS();
    } else if (element_type == INT16_TYPE) {
      XEASSERTALWAYS();
    } else if (element_type == INT32_TYPE || element_type == FLOAT32_TYPE) {
      uint8_t swizzle_mask = static_cast<uint8_t>(i.src2.value);
      swizzle_mask =
          (((swizzle_mask >> 6) & 0x3) << 0) |
          (((swizzle_mask >> 4) & 0x3) << 2) |
          (((swizzle_mask >> 2) & 0x3) << 4) |
          (((swizzle_mask >> 0) & 0x3) << 6);
      e.vpshufd(i.dest, i.src1, swizzle_mask);
    } else if (element_type == INT64_TYPE || element_type == FLOAT64_TYPE) {
      XEASSERTALWAYS();
    } else {
      XEASSERTALWAYS();
    }
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_SWIZZLE,
    SWIZZLE);


// ============================================================================
// OPCODE_PACK
// ============================================================================
EMITTER(PACK, MATCH(I<OPCODE_PACK, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
    case PACK_TYPE_D3DCOLOR:
      EmitD3DCOLOR(e, i);
      break;
    case PACK_TYPE_FLOAT16_2:
      EmitFLOAT16_2(e, i);
      break;
    case PACK_TYPE_FLOAT16_4:
      EmitFLOAT16_4(e, i);
      break;
    case PACK_TYPE_SHORT_2:
      EmitSHORT_2(e, i);
      break;
    case PACK_TYPE_S8_IN_16_LO:
      EmitS8_IN_16_LO(e, i);
      break;
    case PACK_TYPE_S8_IN_16_HI:
      EmitS8_IN_16_HI(e, i);
      break;
    case PACK_TYPE_S16_IN_32_LO:
      EmitS16_IN_32_LO(e, i);
      break;
    case PACK_TYPE_S16_IN_32_HI:
      EmitS16_IN_32_HI(e, i);
      break;
    default: XEASSERTALWAYS(); break;
    }
  }
  static void EmitD3DCOLOR(X64Emitter& e, const EmitArgType& i) {
    // RGBA (XYZW) -> ARGB (WXYZ)
    // float r = roundf(((src1.x < 0) ? 0 : ((1 < src1.x) ? 1 : src1.x)) * 255);
    // float g = roundf(((src1.y < 0) ? 0 : ((1 < src1.y) ? 1 : src1.y)) * 255);
    // float b = roundf(((src1.z < 0) ? 0 : ((1 < src1.z) ? 1 : src1.z)) * 255);
    // float a = roundf(((src1.w < 0) ? 0 : ((1 < src1.w) ? 1 : src1.w)) * 255);
    // dest.iw = ((uint32_t)a << 24) |
    //           ((uint32_t)r << 16) |
    //           ((uint32_t)g << 8) |
    //           ((uint32_t)b);
    // f2i(clamp(src, 0, 1) * 255)
    e.vpxor(e.xmm0, e.xmm0);
    if (i.src1.is_constant) {
      e.LoadConstantXmm(e.xmm1, i.src1.constant());
      e.vmaxps(e.xmm0, e.xmm1);
    } else {
      e.vmaxps(e.xmm0, i.src1);
    }
    e.vminps(e.xmm0, e.GetXmmConstPtr(XMMOne));
    e.vmulps(e.xmm0, e.GetXmmConstPtr(XMM255));
    e.vcvttps2dq(e.xmm0, e.xmm0);
    e.vpshufb(i.dest, e.xmm0, e.GetXmmConstPtr(XMMPackD3DCOLOR));
  }
  static void EmitFLOAT16_2(X64Emitter& e, const EmitArgType& i) {
    XEASSERTALWAYS();
  }
  static void EmitFLOAT16_4(X64Emitter& e, const EmitArgType& i) {
    XEASSERTALWAYS();
  }
  static void EmitSHORT_2(X64Emitter& e, const EmitArgType& i) {
    XEASSERTALWAYS();
  }
  static void EmitS8_IN_16_LO(X64Emitter& e, const EmitArgType& i) {
    XEASSERTALWAYS();
  }
  static void EmitS8_IN_16_HI(X64Emitter& e, const EmitArgType& i) {
    XEASSERTALWAYS();
  }
  static void EmitS16_IN_32_LO(X64Emitter& e, const EmitArgType& i) {
    XEASSERTALWAYS();
  }
  static void EmitS16_IN_32_HI(X64Emitter& e, const EmitArgType& i) {
    XEASSERTALWAYS();
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_PACK,
    PACK);


// ============================================================================
// OPCODE_UNPACK
// ============================================================================
EMITTER(UNPACK, MATCH(I<OPCODE_UNPACK, V128<>, V128<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
    case PACK_TYPE_D3DCOLOR:
      EmitD3DCOLOR(e, i);
      break;
    case PACK_TYPE_FLOAT16_2:
      EmitFLOAT16_2(e, i);
      break;
    case PACK_TYPE_FLOAT16_4:
      EmitFLOAT16_4(e, i);
      break;
    case PACK_TYPE_SHORT_2:
      EmitSHORT_2(e, i);
      break;
    case PACK_TYPE_S8_IN_16_LO:
      EmitS8_IN_16_LO(e, i);
      break;
    case PACK_TYPE_S8_IN_16_HI:
      EmitS8_IN_16_HI(e, i);
      break;
    case PACK_TYPE_S16_IN_32_LO:
      EmitS16_IN_32_LO(e, i);
      break;
    case PACK_TYPE_S16_IN_32_HI:
      EmitS16_IN_32_HI(e, i);
      break;
    default: XEASSERTALWAYS(); break;
    }
  }
  static void EmitD3DCOLOR(X64Emitter& e, const EmitArgType& i) {
    // ARGB (WXYZ) -> RGBA (XYZW)
    // XMLoadColor
    // int32_t src = (int32_t)src1.iw;
    // dest.f4[0] = (float)((src >> 16) & 0xFF) * (1.0f / 255.0f);
    // dest.f4[1] = (float)((src >> 8) & 0xFF) * (1.0f / 255.0f);
    // dest.f4[2] = (float)(src & 0xFF) * (1.0f / 255.0f);
    // dest.f4[3] = (float)((src >> 24) & 0xFF) * (1.0f / 255.0f);

    // src = ZZYYXXWW
    // unpack to 000000ZZ,000000YY,000000XX,000000WW
    e.vpshufb(i.dest, i.src1, e.GetXmmConstPtr(XMMUnpackD3DCOLOR));
    // int -> float
    e.vcvtdq2ps(i.dest, i.dest);
    // mult by 1/255
    e.vmulps(i.dest, e.GetXmmConstPtr(XMMOneOver255));
  }
  static void Unpack_FLOAT16_2(void* raw_context, __m128& v) {
    uint32_t src = v.m128_i32[3];
    v.m128_f32[0] = DirectX::PackedVector::XMConvertHalfToFloat((uint16_t)src);
    v.m128_f32[1] = DirectX::PackedVector::XMConvertHalfToFloat((uint16_t)(src >> 16));
    v.m128_f32[2] = 0.0f;
    v.m128_f32[3] = 1.0f;
  }
  static void EmitFLOAT16_2(X64Emitter& e, const EmitArgType& i) {
    // 1 bit sign, 5 bit exponent, 10 bit mantissa
    // D3D10 half float format
    // TODO(benvanik): http://blogs.msdn.com/b/chuckw/archive/2012/09/11/directxmath-f16c-and-fma.aspx
    // Use _mm_cvtph_ps -- requires very modern processors (SSE5+)
    // Unpacking half floats: http://fgiesen.wordpress.com/2012/03/28/half-to-float-done-quic/
    // Packing half floats: https://gist.github.com/rygorous/2156668
    // Load source, move from tight pack of X16Y16.... to X16...Y16...
    // Also zero out the high end.
    // TODO(benvanik): special case constant unpacks that just get 0/1/etc.

    // sx = src.iw >> 16;
    // sy = src.iw & 0xFFFF;
    // dest = { XMConvertHalfToFloat(sx),
    //          XMConvertHalfToFloat(sy),
    //          0.0,
    //          1.0 };
    auto addr = e.StashXmm(i.src1);
    e.lea(e.rdx, addr);
    e.CallNative(Unpack_FLOAT16_2);
    e.vmovaps(i.dest, addr);
  }
  static void EmitFLOAT16_4(X64Emitter& e, const EmitArgType& i) {
    // Could be shared with FLOAT16_2.
    XEASSERTALWAYS();
  }
  static void EmitSHORT_2(X64Emitter& e, const EmitArgType& i) {
    // (VD.x) = 3.0 + (VB.x>>16)*2^-22
    // (VD.y) = 3.0 + (VB.x)*2^-22
    // (VD.z) = 0.0
    // (VD.w) = 1.0

    // XMLoadShortN2 plus 3,3,0,3 (for some reason)
    // src is (xx,xx,xx,VALUE)
    // (VALUE,VALUE,VALUE,VALUE)
    if (i.src1.is_constant) {
      if (i.src1.value->IsConstantZero()) {
        e.vpxor(i.dest, i.dest);
      } else {
        // TODO(benvanik): check other common constants.
        e.LoadConstantXmm(i.dest, i.src1.constant());
        e.vbroadcastss(i.dest, i.src1);
      }
    } else {
      e.vbroadcastss(i.dest, i.src1);
    }
    // (VALUE&0xFFFF,VALUE&0xFFFF0000,0,0)
    e.vandps(i.dest, e.GetXmmConstPtr(XMMMaskX16Y16));
    // Sign extend.
    e.vxorps(i.dest, e.GetXmmConstPtr(XMMFlipX16Y16));
    // Convert int->float.
    e.cvtpi2ps(i.dest, e.StashXmm(i.dest));
    // 0x8000 to undo sign.
    e.vaddps(i.dest, e.GetXmmConstPtr(XMMFixX16Y16));
    // Normalize.
    e.vmulps(i.dest, e.GetXmmConstPtr(XMMNormalizeX16Y16));
    // Clamp.
    e.vmaxps(i.dest, e.GetXmmConstPtr(XMMNegativeOne));
    // Add 3,3,0,1.
    e.vaddps(i.dest, e.GetXmmConstPtr(XMM3301));
  }
  static void EmitS8_IN_16_LO(X64Emitter& e, const EmitArgType& i) {
    XEASSERTALWAYS();
  }
  static void EmitS8_IN_16_HI(X64Emitter& e, const EmitArgType& i) {
    XEASSERTALWAYS();
  }
  static void EmitS16_IN_32_LO(X64Emitter& e, const EmitArgType& i) {
    XEASSERTALWAYS();
  }
  static void EmitS16_IN_32_HI(X64Emitter& e, const EmitArgType& i) {
    XEASSERTALWAYS();
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_UNPACK,
    UNPACK);


// ============================================================================
// OPCODE_COMPARE_EXCHANGE
// ============================================================================


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
EMITTER(ATOMIC_EXCHANGE_I8, MATCH(I<OPCODE_ATOMIC_EXCHANGE, I8<>, I64<>, I8<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAtomicExchangeXX<ATOMIC_EXCHANGE_I8, Reg8>(e, i);
  }
};
EMITTER(ATOMIC_EXCHANGE_I16, MATCH(I<OPCODE_ATOMIC_EXCHANGE, I16<>, I64<>, I16<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAtomicExchangeXX<ATOMIC_EXCHANGE_I16, Reg16>(e, i);
  }
};
EMITTER(ATOMIC_EXCHANGE_I32, MATCH(I<OPCODE_ATOMIC_EXCHANGE, I32<>, I64<>, I32<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAtomicExchangeXX<ATOMIC_EXCHANGE_I32, Reg32>(e, i);
  }
};
EMITTER(ATOMIC_EXCHANGE_I64, MATCH(I<OPCODE_ATOMIC_EXCHANGE, I64<>, I64<>, I64<>>)) {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAtomicExchangeXX<ATOMIC_EXCHANGE_I64, Reg64>(e, i);
  }
};
EMITTER_OPCODE_TABLE(
    OPCODE_ATOMIC_EXCHANGE,
    ATOMIC_EXCHANGE_I8,
    ATOMIC_EXCHANGE_I16,
    ATOMIC_EXCHANGE_I32,
    ATOMIC_EXCHANGE_I64);


// ============================================================================
// OPCODE_ATOMIC_ADD
// ============================================================================


// ============================================================================
// OPCODE_ATOMIC_SUB
// ============================================================================




//SEQUENCE(ADD_ADD_BRANCH, MATCH(
//    I<OPCODE_ADD, I32<TAG0>, I32<>, I32C<>>,
//    I<OPCODE_ADD, I32<>, I32<TAG0>, I32C<>>,
//    I<OPCODE_BRANCH_TRUE, VoidOp, OffsetOp>)) {
//  static void Emit(X64Emitter& e, const EmitArgs& _) {
//  }
//};



void alloy::backend::x64::RegisterSequences() {
  #define REGISTER_EMITTER_OPCODE_TABLE(opcode) Register_##opcode()
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMMENT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_NOP);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_SOURCE_OFFSET);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_DEBUG_BREAK);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_DEBUG_BREAK_TRUE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_TRAP);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_TRAP_TRUE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_CALL);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_CALL_TRUE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_CALL_INDIRECT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_CALL_INDIRECT_TRUE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_CALL_EXTERN);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_RETURN);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_RETURN_TRUE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_SET_RETURN_ADDRESS);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_BRANCH);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_BRANCH_TRUE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_BRANCH_FALSE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_ASSIGN);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_CAST);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_ZERO_EXTEND);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_SIGN_EXTEND);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_TRUNCATE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_CONVERT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_ROUND);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_VECTOR_CONVERT_I2F);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_VECTOR_CONVERT_F2I);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_LOAD_VECTOR_SHL);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_LOAD_VECTOR_SHR);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_LOAD_CLOCK);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_LOAD_LOCAL);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_STORE_LOCAL);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_LOAD_CONTEXT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_STORE_CONTEXT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_LOAD);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_STORE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_PREFETCH);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_MAX);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_MIN);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_SELECT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_IS_TRUE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_IS_FALSE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_EQ);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_NE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_SLT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_SLE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_SGT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_SGE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_ULT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_ULE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_UGT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_UGE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_SLT_FLT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_SLE_FLT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_SGT_FLT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_SGE_FLT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_ULT_FLT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_ULE_FLT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_UGT_FLT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_UGE_FLT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_DID_CARRY);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_DID_OVERFLOW);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_DID_SATURATE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_EQ);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_SGT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_SGE);
  //REGISTER_EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_UGT);
  //REGISTER_EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_UGE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_ADD);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_ADD_CARRY);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_VECTOR_ADD);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_SUB);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_MUL);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_MUL_HI);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_DIV);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_MUL_ADD);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_MUL_SUB);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_NEG);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_ABS);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_SQRT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_RSQRT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_POW2);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_LOG2);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_DOT_PRODUCT_3);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_DOT_PRODUCT_4);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_AND);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_OR);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_XOR);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_NOT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_SHL);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_SHR);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_SHA);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SHL);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SHR);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SHA);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_ROTATE_LEFT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_BYTE_SWAP);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_CNTLZ);
  //REGISTER_EMITTER_OPCODE_TABLE(OPCODE_INSERT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_EXTRACT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_SPLAT);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_PERMUTE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_SWIZZLE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_PACK);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_UNPACK);
  //REGISTER_EMITTER_OPCODE_TABLE(OPCODE_COMPARE_EXCHANGE);
  REGISTER_EMITTER_OPCODE_TABLE(OPCODE_ATOMIC_EXCHANGE);
  //REGISTER_EMITTER_OPCODE_TABLE(OPCODE_ATOMIC_ADD);
  //REGISTER_EMITTER_OPCODE_TABLE(OPCODE_ATOMIC_SUB);
}

bool alloy::backend::x64::SelectSequence(X64Emitter& e, const Instr* i, const Instr** new_tail) {
  const InstrKey key(i);
  const auto its = sequence_table.equal_range(key);
  for (auto it = its.first; it != its.second; ++it) {
    if (it->second(e, i, new_tail)) {
      return true;
    }
  }
  XELOGE("No sequence match for variant %s", i->opcode->name);
  return false;
}
