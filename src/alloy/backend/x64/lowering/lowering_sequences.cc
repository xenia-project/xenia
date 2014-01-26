/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/lowering/lowering_sequences.h>

#include <alloy/backend/x64/x64_emitter.h>
#include <alloy/backend/x64/lowering/lowering_table.h>

using namespace alloy;
using namespace alloy::backend::x64;
using namespace alloy::backend::x64::lowering;
using namespace alloy::hir;

using namespace Xbyak;


namespace {

#define UNIMPLEMENTED_SEQ() __debugbreak()
#define ASSERT_INVALID_TYPE() XEASSERTALWAYS()

// TODO(benvanik): emit traces/printfs/etc

void Dummy() {
  //
}

// Sets EFLAGs with zf for the given value.
void CheckBoolean(X64Emitter& e, Value* v) {
  if (v->IsConstant()) {
    e.mov(e.ah, (v->IsConstantZero() ? 1 : 0) << 6);
    e.sahf();
  } else if (v->type == INT8_TYPE) {
    Reg8 src;
    e.BeginOp(v, src, 0);
    e.test(src, src);
    e.EndOp(src);
  } else if (v->type == INT16_TYPE) {
    Reg16 src;
    e.BeginOp(v, src, 0);
    e.test(src, src);
    e.EndOp(src);
  } else if (v->type == INT32_TYPE) {
    Reg32 src;
    e.BeginOp(v, src, 0);
    e.test(src, src);
    e.EndOp(src);
  } else if (v->type == INT64_TYPE) {
    Reg64 src;
    e.BeginOp(v, src, 0);
    e.test(src, src);
    e.EndOp(src);
  } else if (v->type == FLOAT32_TYPE) {
    UNIMPLEMENTED_SEQ();
  } else if (v->type == FLOAT64_TYPE) {
    UNIMPLEMENTED_SEQ();
  } else if (v->type == VEC128_TYPE) {
    UNIMPLEMENTED_SEQ();
  } else {
    ASSERT_INVALID_TYPE();
  }
}

void CompareXX(X64Emitter& e, Instr*& i, void(set_fn)(X64Emitter& e, Reg8& dest, bool invert)) {
  if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I8, SIG_TYPE_I8)) {
    Reg8 dest;
    Reg8 src1, src2;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0,
              i->src2.value, src2, 0);
    e.cmp(src1, src2);
    set_fn(e, dest, false);
    e.EndOp(dest, src1, src2);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I8, SIG_TYPE_I8C)) {
    Reg8 dest;
    Reg8 src1;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0);
    e.cmp(src1, i->src2.value->constant.i8);
    set_fn(e, dest, false);
    e.EndOp(dest, src1);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I8C, SIG_TYPE_I8)) {
    Reg8 dest;
    Reg8 src2;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src2.value, src2, 0);
    e.cmp(src2, i->src1.value->constant.i8);
    set_fn(e, dest, true);
    e.EndOp(dest, src2);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I16, SIG_TYPE_I16)) {
    Reg8 dest;
    Reg16 src1, src2;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0,
              i->src2.value, src2, 0);
    e.cmp(src1, src2);
    set_fn(e, dest, false);
    e.EndOp(dest, src1, src2);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I16, SIG_TYPE_I16C)) {
    Reg8 dest;
    Reg16 src1;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0);
    e.cmp(src1, i->src2.value->constant.i16);
    set_fn(e, dest, false);
    e.EndOp(dest, src1);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I16C, SIG_TYPE_I16)) {
    Reg8 dest;
    Reg16 src2;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src2.value, src2, 0);
    e.cmp(src2, i->src1.value->constant.i16);
    e.sete(dest);
    set_fn(e, dest, true);
    e.EndOp(dest, src2);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I32, SIG_TYPE_I32)) {
    Reg8 dest;
    Reg32 src1, src2;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0,
              i->src2.value, src2, 0);
    e.cmp(src1, src2);
    set_fn(e, dest, false);
    e.EndOp(dest, src1, src2);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I32, SIG_TYPE_I32C)) {
    Reg8 dest;
    Reg32 src1;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0);
    e.cmp(src1, i->src2.value->constant.i32);
    set_fn(e, dest, false);
    e.EndOp(dest, src1);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I32C, SIG_TYPE_I32)) {
    Reg8 dest;
    Reg32 src2;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src2.value, src2, 0);
    e.cmp(src2, i->src1.value->constant.i32);
    set_fn(e, dest, true);
    e.EndOp(dest, src2);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I64, SIG_TYPE_I64)) {
    Reg8 dest;
    Reg64 src1, src2;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0,
              i->src2.value, src2, 0);
    e.cmp(src1, src2);
    set_fn(e, dest, false);
    e.EndOp(dest, src1, src2);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I64, SIG_TYPE_I64C)) {
    Reg8 dest;
    Reg64 src1;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0);
    e.mov(e.rax, i->src2.value->constant.i64);
    e.cmp(src1, e.rax);
    set_fn(e, dest, false);
    e.EndOp(dest, src1);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I64C, SIG_TYPE_I64)) {
    Reg8 dest;
    Reg64 src2;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src2.value, src2, 0);
    e.mov(e.rax, i->src1.value->constant.i64);
    e.cmp(src2, e.rax);
    set_fn(e, dest, true);
    e.EndOp(dest, src2);
  } else {
    UNIMPLEMENTED_SEQ();
  }
};

}  // namespace


void alloy::backend::x64::lowering::RegisterSequences(LoweringTable* table) {
  // --------------------------------------------------------------------------
  // General
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_COMMENT, [](X64Emitter& e, Instr*& i) {
    // TODO(benvanik): pass through.
    auto str = (const char*)i->src1.offset;
    //lb.Comment(str);
    //UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_NOP, [](X64Emitter& e, Instr*& i) {
    // If we got this, chances are we want it.
    e.nop();
    i = e.Advance(i);
    return true;
  });

  // --------------------------------------------------------------------------
  // Debugging
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_SOURCE_OFFSET, [](X64Emitter& e, Instr*& i) {
    // TODO(benvanik): translate source offsets for mapping? We're just passing
    //     down the original offset - it may be nice to have two.
    e.MarkSourceOffset(i);
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_DEBUG_BREAK, [](X64Emitter& e, Instr*& i) {
    // TODO(benvanik): insert a call to the debug break function to let the
    //     debugger know.
    e.db(0xCC);
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_DEBUG_BREAK_TRUE, [](X64Emitter& e, Instr*& i) {
    e.inLocalLabel();
    CheckBoolean(e, i->src1.value);
    e.jne(".x", e.T_SHORT);
    // TODO(benvanik): insert a call to the debug break function to let the
    //     debugger know.
    e.db(0xCC);
    e.L(".x");
    e.outLocalLabel();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_TRAP, [](X64Emitter& e, Instr*& i) {
    // TODO(benvanik): insert a call to the trap function to let the
    //     debugger know.
    e.db(0xCC);
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_TRAP_TRUE, [](X64Emitter& e, Instr*& i) {
    e.inLocalLabel();
    CheckBoolean(e, i->src1.value);
    e.jne(".x", e.T_SHORT);
    // TODO(benvanik): insert a call to the trap function to let the
    //     debugger know.
    e.db(0xCC);
    e.L(".x");
    e.outLocalLabel();
    i = e.Advance(i);
    return true;
  });

  // --------------------------------------------------------------------------
  // Calls
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_CALL, [](X64Emitter& e, Instr*& i) {
    e.mov(e.rax, (uint64_t)Dummy);
    e.call(e.rax);
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_CALL_TRUE, [](X64Emitter& e, Instr*& i) {
    e.inLocalLabel();
    CheckBoolean(e, i->src1.value);
    e.jne(".x", e.T_SHORT);
    // TODO(benvanik): call
    e.mov(e.rax, (uint64_t)Dummy);
    e.call(e.rax);
    UNIMPLEMENTED_SEQ();
    e.L(".x");
    e.outLocalLabel();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_CALL_INDIRECT, [](X64Emitter& e, Instr*& i) {
    e.mov(e.rax, (uint64_t)Dummy);
    e.call(e.rax);
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_CALL_INDIRECT_TRUE, [](X64Emitter& e, Instr*& i) {
    e.inLocalLabel();
    CheckBoolean(e, i->src1.value);
    e.jne(".x", e.T_SHORT);
    // TODO(benvanik): call
    e.mov(e.rax, (uint64_t)Dummy);
    e.call(e.rax);
    UNIMPLEMENTED_SEQ();
    e.L(".x");
    e.outLocalLabel();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_RETURN, [](X64Emitter& e, Instr*& i) {
    e.ret();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_SET_RETURN_ADDRESS, [](X64Emitter& e, Instr*& i) {
    //UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  // --------------------------------------------------------------------------
  // Branches
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_BRANCH, [](X64Emitter& e, Instr*& i) {
    auto target = i->src1.label;
    e.jmp(target->name, e.T_NEAR);
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_BRANCH_TRUE, [](X64Emitter& e, Instr*& i) {
    CheckBoolean(e, i->src1.value);
    auto target = i->src2.label;
    e.je(target->name, e.T_NEAR);
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_BRANCH_FALSE, [](X64Emitter& e, Instr*& i) {
    CheckBoolean(e, i->src1.value);
    auto target = i->src2.label;
    e.jne(target->name, e.T_NEAR);
    i = e.Advance(i);
    return true;
  });

  // --------------------------------------------------------------------------
  // Types
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_ASSIGN, [](X64Emitter& e, Instr*& i) {
    if (i->dest->type == INT8_TYPE) {
      Reg8 dest, src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.mov(dest, src);
      e.EndOp(dest, src);
    } else if (i->dest->type == INT16_TYPE) {
      Reg16 dest, src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.mov(dest, src);
      e.EndOp(dest, src);
    } else if (i->dest->type == INT32_TYPE) {
      Reg32 dest, src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.mov(dest, src);
      e.EndOp(dest, src);
    } else if (i->dest->type == INT64_TYPE) {
      Reg64 dest, src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.mov(dest, src);
      e.EndOp(dest, src);
    } else if (i->dest->type == FLOAT32_TYPE) {
      UNIMPLEMENTED_SEQ();
    } else if (i->dest->type == FLOAT64_TYPE) {
      UNIMPLEMENTED_SEQ();
    } else if (i->dest->type == VEC128_TYPE) {
      UNIMPLEMENTED_SEQ();
    } else {
      ASSERT_INVALID_TYPE();
    }
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_CAST, [](X64Emitter& e, Instr*& i) {
    // Need a matrix.
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_ZERO_EXTEND, [](X64Emitter& e, Instr*& i) {
    if (i->Match(SIG_TYPE_I16, SIG_TYPE_I8)) {
      Reg16 dest;
      Reg8 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.movzx(dest, src.cvt8());
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I8)) {
      Reg32 dest;
      Reg8 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.movzx(dest, src.cvt8());
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I16)) {
      Reg32 dest;
      Reg16 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.movzx(dest, src.cvt8());
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I8)) {
      Reg64 dest;
      Reg8 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.movzx(dest, src.cvt16());
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I16)) {
      Reg64 dest;
      Reg16 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.movzx(dest, src.cvt16());
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I32)) {
      Reg64 dest;
      Reg32 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.mov(dest.cvt32(), src.cvt32());
      e.EndOp(dest, src);
    } else {
      UNIMPLEMENTED_SEQ();
    }
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_SIGN_EXTEND, [](X64Emitter& e, Instr*& i) {
    if (i->Match(SIG_TYPE_I16, SIG_TYPE_I8)) {
      Reg16 dest;
      Reg8 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.movsx(dest, src.cvt8());
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I8)) {
      Reg32 dest;
      Reg8 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.movsx(dest, src.cvt8());
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I16)) {
      Reg32 dest;
      Reg16 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.movsx(dest, src.cvt8());
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I8)) {
      Reg64 dest;
      Reg8 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.movsx(dest, src.cvt16());
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I16)) {
      Reg64 dest;
      Reg16 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.movsx(dest, src.cvt16());
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I32)) {
      Reg64 dest;
      Reg32 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.movsx(dest, src.cvt32());
      e.EndOp(dest, src);
    } else {
      UNIMPLEMENTED_SEQ();
    }
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_TRUNCATE, [](X64Emitter& e, Instr*& i) {
    if (i->Match(SIG_TYPE_I8, SIG_TYPE_I16)) {
      Reg8 dest;
      Reg16 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.mov(dest, src.cvt8());
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_I8, SIG_TYPE_I32)) {
      Reg8 dest;
      Reg16 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.mov(dest, src.cvt8());
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_I8, SIG_TYPE_I64)) {
      Reg8 dest;
      Reg64 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.mov(dest, src.cvt8());
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I32)) {
      Reg16 dest;
      Reg32 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.mov(dest, src.cvt16());
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I64)) {
      Reg16 dest;
      Reg64 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.mov(dest, src.cvt16());
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I64)) {
      Reg32 dest;
      Reg64 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.mov(dest, src.cvt32());
      e.EndOp(dest, src);
    } else {
      UNIMPLEMENTED_SEQ();
    }
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_CONVERT, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_ROUND, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_CONVERT_I2F, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_CONVERT_F2I, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  // --------------------------------------------------------------------------
  // Constants
  // --------------------------------------------------------------------------

  // specials for zeroing/etc (xor/etc)

  table->AddSequence(OPCODE_LOAD_VECTOR_SHL, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_LOAD_VECTOR_SHR, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_LOAD_CLOCK, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  // --------------------------------------------------------------------------
  // Context
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_LOAD_CONTEXT, [](X64Emitter& e, Instr*& i) {
    if (i->Match(SIG_TYPE_I8, SIG_TYPE_IGNORE)) {
      Reg8 dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      e.mov(dest, e.byte[e.rcx + i->src1.offset]);
      e.EndOp(dest);
    } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_IGNORE)) {
      Reg16 dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      e.mov(dest, e.word[e.rcx + i->src1.offset]);
      e.EndOp(dest);
    } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_IGNORE)) {
      Reg32 dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      e.mov(dest, e.dword[e.rcx + i->src1.offset]);
      e.EndOp(dest);
    } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_IGNORE)) {
      Reg64 dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      e.mov(dest, e.qword[e.rcx + i->src1.offset]);
      e.EndOp(dest);
    } else if (i->Match(SIG_TYPE_F32, SIG_TYPE_IGNORE)) {
      Xmm dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      e.movss(dest, e.dword[e.rcx + i->src1.offset]);
      e.EndOp(dest);
    } else if (i->Match(SIG_TYPE_F64, SIG_TYPE_IGNORE)) {
      Xmm dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      e.movsd(dest, e.qword[e.rcx + i->src1.offset]);
      e.EndOp(dest);
    } else if (i->Match(SIG_TYPE_V128, SIG_TYPE_IGNORE)) {
      Xmm dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      // TODO(benvanik): we should try to stick to movaps if possible.
      e.movups(dest, e.ptr[e.rcx + i->src1.offset]);
      e.EndOp(dest);
    } else {
      ASSERT_INVALID_TYPE();
    }
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_STORE_CONTEXT, [](X64Emitter& e, Instr*& i) {
    if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I8)) {
      Reg8 src;
      e.BeginOp(i->src2.value, src, 0);
      e.mov(e.byte[e.rcx + i->src1.offset], src);
      e.EndOp(src);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I8C)) {
      e.mov(e.byte[e.rcx + i->src1.offset], i->src2.value->constant.i8);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I16)) {
      Reg16 src;
      e.BeginOp(i->src2.value, src, 0);
      e.mov(e.word[e.rcx + i->src1.offset], src);
      e.EndOp(src);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I16C)) {
      e.mov(e.word[e.rcx + i->src1.offset], i->src2.value->constant.i16);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I32)) {
      Reg32 src;
      e.BeginOp(i->src2.value, src, 0);
      e.mov(e.dword[e.rcx + i->src1.offset], src);
      e.EndOp(src);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I32C)) {
      e.mov(e.dword[e.rcx + i->src1.offset], i->src2.value->constant.i32);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I64)) {
      Reg64 src;
      e.BeginOp(i->src2.value, src, 0);
      e.mov(e.qword[e.rcx + i->src1.offset], src);
      e.EndOp(src);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I64C)) {
      e.mov(e.qword[e.rcx + i->src1.offset], i->src2.value->constant.i64);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F32)) {
      Xmm src;
      e.BeginOp(i->src2.value, src, 0);
      e.movss(e.dword[e.rcx + i->src1.offset], src);
      e.EndOp(src);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F32C)) {
      e.mov(e.dword[e.rcx + i->src1.offset], i->src2.value->constant.i32);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F64)) {
      Xmm src;
      e.BeginOp(i->src2.value, src, 0);
      e.movsd(e.qword[e.rcx + i->src1.offset], src);
      e.EndOp(src);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F64C)) {
      e.mov(e.qword[e.rcx + i->src1.offset], i->src2.value->constant.i64);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_V128)) {
      Xmm src;
      e.BeginOp(i->src2.value, src, 0);
      // NOTE: we always know we are aligned.
      e.movaps(e.ptr[e.rcx + i->src1.offset], src);
      e.EndOp(src);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_V128C)) {
      e.mov(e.ptr[e.rcx + i->src1.offset], i->src2.value->constant.v128.low);
      e.mov(e.ptr[e.rcx + i->src1.offset + 8], i->src2.value->constant.v128.high);
    } else {
      ASSERT_INVALID_TYPE();
    }
    i = e.Advance(i);
    return true;
  });

  // --------------------------------------------------------------------------
  // Memory
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_LOAD, [](X64Emitter& e, Instr*& i) {
    // TODO(benvanik): dynamic register access check.
    // mov reg, [membase + address.32]
    Reg64 addr_off;
    RegExp addr;
    if (i->src1.value->IsConstant()) {
      // TODO(benvanik): a way to do this without using a register.
      e.mov(e.eax, i->src1.value->AsUint32());
      addr = e.rdx + e.rax;
    } else {
      e.BeginOp(i->src1.value, addr_off, 0);
      e.mov(addr_off.cvt32(), addr_off.cvt32()); // trunc to 32bits
      addr = e.rdx + addr_off;
    }
    if (i->Match(SIG_TYPE_I8, SIG_TYPE_IGNORE)) {
      Reg8 dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      e.mov(dest, e.byte[addr]);
      e.EndOp(dest);
    } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_IGNORE)) {
      Reg16 dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      e.mov(dest, e.word[addr]);
      e.EndOp(dest);
    } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_IGNORE)) {
      Reg32 dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      e.mov(dest, e.dword[addr]);
      e.EndOp(dest);
    } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_IGNORE)) {
      Reg64 dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      e.mov(dest, e.qword[addr]);
      e.EndOp(dest);
    } else if (i->Match(SIG_TYPE_F32, SIG_TYPE_IGNORE)) {
      Xmm dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      e.movss(dest, e.dword[addr]);
      e.EndOp(dest);
    } else if (i->Match(SIG_TYPE_F64, SIG_TYPE_IGNORE)) {
      Xmm dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      e.movsd(dest, e.qword[addr]);
      e.EndOp(dest);
    } else if (i->Match(SIG_TYPE_V128, SIG_TYPE_IGNORE)) {
      Xmm dest;
      e.BeginOp(i->dest, dest, REG_DEST);
      // TODO(benvanik): we should try to stick to movaps if possible.
      e.movups(dest, e.ptr[addr]);
      e.EndOp(dest);
    } else {
      ASSERT_INVALID_TYPE();
    }
    if (!i->src1.value->IsConstant()) {
      e.EndOp(addr_off);
    }
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_STORE, [](X64Emitter& e, Instr*& i) {
    // TODO(benvanik): dynamic register access check
    // mov [membase + address.32], reg
    Reg64 addr_off;
    RegExp addr;
    if (i->src1.value->IsConstant()) {
      e.mov(e.eax, i->src1.value->AsUint32());
      addr = e.rdx + e.rax;
    } else {
      e.BeginOp(i->src1.value, addr_off, 0);
      e.mov(addr_off.cvt32(), addr_off.cvt32()); // trunc to 32bits
      addr = e.rdx + addr_off;
    }
    if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I8)) {
      Reg8 src;
      e.BeginOp(i->src2.value, src, 0);
      e.mov(e.byte[addr], src);
      e.EndOp(src);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I8C)) {
      e.mov(e.byte[addr], i->src2.value->constant.i8);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I16)) {
      Reg16 src;
      e.BeginOp(i->src2.value, src, 0);
      e.mov(e.word[addr], src);
      e.EndOp(src);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I16C)) {
      e.mov(e.word[addr], i->src2.value->constant.i16);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I32)) {
      Reg32 src;
      e.BeginOp(i->src2.value, src, 0);
      e.mov(e.dword[addr], src);
      e.EndOp(src);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I32C)) {
      e.mov(e.dword[addr], i->src2.value->constant.i32);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I64)) {
      Reg64 src;
      e.BeginOp(i->src2.value, src, 0);
      e.mov(e.qword[addr], src);
      e.EndOp(src);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_I64C)) {
      e.mov(e.qword[addr], i->src2.value->constant.i64);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F32)) {
      Xmm src;
      e.BeginOp(i->src2.value, src, 0);
      e.movss(e.dword[addr], src);
      e.EndOp(src);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F32C)) {
      e.mov(e.dword[addr], i->src2.value->constant.i32);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F64)) {
      Xmm src;
      e.BeginOp(i->src2.value, src, 0);
      e.movsd(e.qword[addr], src);
      e.EndOp(src);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_F64C)) {
      e.mov(e.qword[addr], i->src2.value->constant.i64);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_V128)) {
      Xmm src;
      e.BeginOp(i->src2.value, src, 0);
      // TODO(benvanik): we should try to stick to movaps if possible.
      e.movups(e.ptr[addr], src);
      e.EndOp(src);
    } else if (i->Match(SIG_TYPE_X, SIG_TYPE_IGNORE, SIG_TYPE_V128C)) {
      e.mov(e.ptr[addr], i->src2.value->constant.v128.low);
      e.mov(e.ptr[addr + 8], i->src2.value->constant.v128.high);
    } else {
      ASSERT_INVALID_TYPE();
    }
    if (!i->src1.value->IsConstant()) {
      e.EndOp(addr_off);
    }
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_PREFETCH, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  // --------------------------------------------------------------------------
  // Comparisons
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_MAX, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_MIN, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_SELECT, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_IS_TRUE, [](X64Emitter& e, Instr*& i) {
    CheckBoolean(e, i->src1.value);
    Reg8 dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.setnz(dest);
    e.EndOp(dest);
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_IS_FALSE, [](X64Emitter& e, Instr*& i) {
    CheckBoolean(e, i->src1.value);
    Reg8 dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.setz(dest);
    e.EndOp(dest);
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_EQ, [](X64Emitter& e, Instr*& i) {
    CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
      if (!invert) {
        e.sete(dest);
      } else {
        e.setne(dest);
      }
    });
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_NE, [](X64Emitter& e, Instr*& i) {
    CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
      if (!invert) {
        e.setne(dest);
      } else {
        e.sete(dest);
      }
    });
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_SLT, [](X64Emitter& e, Instr*& i) {
    CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
      if (!invert) {
        e.setl(dest);
      } else {
        e.setge(dest);
      }
    });
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_SLE, [](X64Emitter& e, Instr*& i) {
    CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
      if (!invert) {
        e.setle(dest);
      } else {
        e.setg(dest);
      }
    });
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_SGT, [](X64Emitter& e, Instr*& i) {
    CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
      if (!invert) {
        e.setg(dest);
      } else {
        e.setle(dest);
      }
    });
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_SGE, [](X64Emitter& e, Instr*& i) {
    CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
      if (!invert) {
        e.setge(dest);
      } else {
        e.setl(dest);
      }
    });
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_ULT, [](X64Emitter& e, Instr*& i) {
    CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
      if (!invert) {
        e.setb(dest);
      } else {
        e.setae(dest);
      }
    });
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_ULE, [](X64Emitter& e, Instr*& i) {
    CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
      if (!invert) {
        e.setbe(dest);
      } else {
        e.seta(dest);
      }
    });
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_UGT, [](X64Emitter& e, Instr*& i) {
    CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
      if (!invert) {
        e.seta(dest);
      } else {
        e.setbe(dest);
      }
    });
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_COMPARE_UGE, [](X64Emitter& e, Instr*& i) {
    CompareXX(e, i, [](X64Emitter& e, Reg8& dest, bool invert) {
      if (!invert) {
        e.setae(dest);
      } else {
        e.setb(dest);
      }
    });
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_DID_CARRY, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_DID_OVERFLOW, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_DID_SATURATE, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_COMPARE_EQ, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_COMPARE_SGT, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_COMPARE_SGE, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_COMPARE_UGT, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_COMPARE_UGE, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  // --------------------------------------------------------------------------
  // Math
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_ADD, [](X64Emitter& e, Instr*& i) {
    if (i->Match(SIG_TYPE_I8, SIG_TYPE_I8, SIG_TYPE_I8)) {
      Reg8 dest, src1, src2;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0,
                i->src2.value, src2, 0);
      if (dest == src1) {
        e.add(dest, src2);
      } else if (dest == src2) {
        e.add(dest, src1);
      } else {
        e.mov(dest, src1);
        e.add(dest, src2);
      }
      if (i->flags & ARITHMETIC_SET_CARRY) {
        UNIMPLEMENTED_SEQ();
      }
      e.EndOp(dest, src1, src2);
    } else if (i->Match(SIG_TYPE_I8, SIG_TYPE_I8, SIG_TYPE_I8C)) {
      Reg8 dest, src1;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0);
      if (dest == src1) {
        e.add(dest, i->src2.value->constant.i8);
      } else {
        e.mov(dest, src1);
        e.add(dest, i->src2.value->constant.i8);
      }
      if (i->flags & ARITHMETIC_SET_CARRY) {
        UNIMPLEMENTED_SEQ();
      }
      e.EndOp(dest, src1);
    } else if (i->Match(SIG_TYPE_I8, SIG_TYPE_I8C, SIG_TYPE_I8)) {
      Reg8 dest, src2;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src2.value, src2, 0);
      if (dest == src2) {
        e.add(dest, i->src1.value->constant.i8);
      } else {
        e.mov(dest, src2);
        e.add(dest, i->src1.value->constant.i8);
      }
      if (i->flags & ARITHMETIC_SET_CARRY) {
        UNIMPLEMENTED_SEQ();
      }
      e.EndOp(dest, src2);
    } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16, SIG_TYPE_I16)) {
      Reg16 dest, src1, src2;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0,
                i->src2.value, src2, 0);
      if (dest == src1) {
        e.add(dest, src2);
      } else if (dest == src2) {
        e.add(dest, src1);
      } else {
        e.mov(dest, src1);
        e.add(dest, src2);
      }
      if (i->flags & ARITHMETIC_SET_CARRY) {
        UNIMPLEMENTED_SEQ();
      }
      e.EndOp(dest, src1, src2);
    } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16, SIG_TYPE_I16C)) {
      Reg16 dest, src1;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0);
      if (dest == src1) {
        e.add(dest, i->src2.value->constant.i16);
      } else {
        e.mov(dest, src1);
        e.add(dest, i->src2.value->constant.i16);
      }
      if (i->flags & ARITHMETIC_SET_CARRY) {
        UNIMPLEMENTED_SEQ();
      }
      e.EndOp(dest, src1);
    } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16C, SIG_TYPE_I16)) {
      Reg16 dest, src2;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src2.value, src2, 0);
      if (dest == src2) {
        e.add(dest, i->src1.value->constant.i16);
      } else {
        e.mov(dest, src2);
        e.add(dest, i->src1.value->constant.i16);
      }
      if (i->flags & ARITHMETIC_SET_CARRY) {
        UNIMPLEMENTED_SEQ();
      }
      e.EndOp(dest, src2);
    } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32, SIG_TYPE_I32)) {
      Reg32 dest, src1, src2;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0,
                i->src2.value, src2, 0);
      if (dest == src1) {
        e.add(dest, src2);
      } else if (dest == src2) {
        e.add(dest, src1);
      } else {
        e.mov(dest, src1);
        e.add(dest, src2);
      }
      if (i->flags & ARITHMETIC_SET_CARRY) {
        UNIMPLEMENTED_SEQ();
      }
      e.EndOp(dest, src1, src2);
    } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32, SIG_TYPE_I32C)) {
      Reg32 dest, src1;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0);
      if (dest == src1) {
        e.add(dest, i->src2.value->constant.i32);
      } else {
        e.mov(dest, src1);
        e.add(dest, i->src2.value->constant.i32);
      }
      if (i->flags & ARITHMETIC_SET_CARRY) {
        UNIMPLEMENTED_SEQ();
      }
      e.EndOp(dest, src1);
    } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32C, SIG_TYPE_I32)) {
      Reg32 dest, src2;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src2.value, src2, 0);
      if (dest == src2) {
        e.add(dest, i->src1.value->constant.i32);
      } else {
        e.mov(dest, src2);
        e.add(dest, i->src1.value->constant.i32);
      }
      if (i->flags & ARITHMETIC_SET_CARRY) {
        UNIMPLEMENTED_SEQ();
      }
      e.EndOp(dest, src2);
    } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64, SIG_TYPE_I64)) {
      Reg64 dest, src1, src2;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0,
                i->src2.value, src2, 0);
      if (dest == src1) {
        e.add(dest, src2);
      } else if (dest == src2) {
        e.add(dest, src1);
      } else {
        e.mov(dest, src1);
        e.add(dest, src2);
      }
      if (i->flags & ARITHMETIC_SET_CARRY) {
        UNIMPLEMENTED_SEQ();
      }
      e.EndOp(dest, src1, src2);
    } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64, SIG_TYPE_I64C)) {
      Reg64 dest, src1;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0);
      if (dest == src1) {
        e.mov(e.rax, i->src2.value->constant.i64);
        e.add(dest, e.rax);
      } else {
        e.mov(e.rax, i->src2.value->constant.i64);
        e.mov(dest, src1);
        e.add(dest, e.rax);
      }
      if (i->flags & ARITHMETIC_SET_CARRY) {
        UNIMPLEMENTED_SEQ();
      }
      e.EndOp(dest, src1);
    } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64C, SIG_TYPE_I64)) {
      Reg64 dest, src2;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src2.value, src2, 0);
      if (dest == src2) {
        e.mov(e.rax, i->src1.value->constant.i64);
        e.add(dest, e.rax);
      } else {
        e.mov(e.rax, i->src1.value->constant.i64);
        e.mov(dest, src2);
        e.add(dest, e.rax);
      }
      if (i->flags & ARITHMETIC_SET_CARRY) {
        UNIMPLEMENTED_SEQ();
      }
      e.EndOp(dest, src2);
    } else {
      ASSERT_INVALID_TYPE();
    }
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_ADD_CARRY, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_SUB, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_MUL, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_MUL_HI, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_DIV, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_MUL_ADD, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_MUL_SUB, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_NEG, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_ABS, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_SQRT, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_RSQRT, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_POW2, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_LOG2, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_DOT_PRODUCT_3, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_DOT_PRODUCT_4, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_AND, [](X64Emitter& e, Instr*& i) {
    if (i->Match(SIG_TYPE_I8, SIG_TYPE_I8, SIG_TYPE_I8)) {
      Reg8 dest, src1, src2;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0,
                i->src2.value, src2, 0);
      if (dest == src1) {
        e.and(dest, src2);
      } else if (dest == src2) {
        e.and(dest, src1);
      } else {
        e.mov(dest, src1);
        e.and(dest, src2);
      }
      e.EndOp(dest, src1, src2);
    } else if (i->Match(SIG_TYPE_I8, SIG_TYPE_I8, SIG_TYPE_I8C)) {
      Reg8 dest, src1;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0);
      if (dest == src1) {
        e.and(dest, i->src2.value->constant.i8);
      } else {
        e.mov(dest, src1);
        e.and(dest, i->src2.value->constant.i8);
      }
      e.EndOp(dest, src1);
    } else if (i->Match(SIG_TYPE_I8, SIG_TYPE_I8C, SIG_TYPE_I8)) {
      Reg8 dest, src2;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src2.value, src2, 0);
      if (dest == src2) {
        e.and(dest, i->src1.value->constant.i8);
      } else {
        e.mov(dest, src2);
        e.and(dest, i->src1.value->constant.i8);
      }
      e.EndOp(dest, src2);
    } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16, SIG_TYPE_I16)) {
      Reg16 dest, src1, src2;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0,
                i->src2.value, src2, 0);
      if (dest == src1) {
        e.and(dest, src2);
      } else if (dest == src2) {
        e.and(dest, src1);
      } else {
        e.mov(dest, src1);
        e.and(dest, src2);
      }
      e.EndOp(dest, src1, src2);
    } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16, SIG_TYPE_I16C)) {
      Reg16 dest, src1;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0);
      if (dest == src1) {
        e.and(dest, i->src2.value->constant.i16);
      } else {
        e.mov(dest, src1);
        e.and(dest, i->src2.value->constant.i16);
      }
      e.EndOp(dest, src1);
    } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16C, SIG_TYPE_I16)) {
      Reg16 dest, src2;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src2.value, src2, 0);
      if (dest == src2) {
        e.and(dest, i->src1.value->constant.i16);
      } else {
        e.mov(dest, src2);
        e.and(dest, i->src1.value->constant.i16);
      }
      e.EndOp(dest, src2);
    } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32, SIG_TYPE_I32)) {
      Reg32 dest, src1, src2;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0,
                i->src2.value, src2, 0);
      if (dest == src1) {
        e.and(dest, src2);
      } else if (dest == src2) {
        e.and(dest, src1);
      } else {
        e.mov(dest, src1);
        e.and(dest, src2);
      }
      e.EndOp(dest, src1, src2);
    } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32, SIG_TYPE_I32C)) {
      Reg32 dest, src1;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0);
      if (dest == src1) {
        e.and(dest, i->src2.value->constant.i32);
      } else {
        e.mov(dest, src1);
        e.and(dest, i->src2.value->constant.i32);
      }
      e.EndOp(dest, src1);
    } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32C, SIG_TYPE_I32)) {
      Reg32 dest, src2;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src2.value, src2, 0);
      if (dest == src2) {
        e.and(dest, i->src1.value->constant.i32);
      } else {
        e.mov(dest, src2);
        e.and(dest, i->src1.value->constant.i32);
      }
      e.EndOp(dest, src2);
    } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64, SIG_TYPE_I64)) {
      Reg64 dest, src1, src2;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0,
                i->src2.value, src2, 0);
      if (dest == src1) {
        e.and(dest, src2);
      } else if (dest == src2) {
        e.and(dest, src1);
      } else {
        e.mov(dest, src1);
        e.and(dest, src2);
      }
      e.EndOp(dest, src1, src2);
    } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64, SIG_TYPE_I64C)) {
      Reg64 dest, src1;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0);
      if (dest == src1) {
        e.mov(e.rax, i->src2.value->constant.i64);
        e.and(dest, e.rax);
      } else {
        e.mov(e.rax, i->src2.value->constant.i64);
        e.mov(dest, src1);
        e.and(dest, e.rax);
      }
      e.EndOp(dest, src1);
    } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64C, SIG_TYPE_I64)) {
      Reg64 dest, src2;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src2.value, src2, 0);
      if (dest == src2) {
        e.mov(e.rax, i->src1.value->constant.i64);
        e.and(dest, e.rax);
      } else {
        e.mov(e.rax, i->src1.value->constant.i64);
        e.mov(dest, src2);
        e.and(dest, e.rax);
      }
      e.EndOp(dest, src2);
    } else {
      ASSERT_INVALID_TYPE();
    }
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_OR, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_XOR, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_NOT, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_SHL, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_SHL, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_SHR, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_SHR, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_SHA, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_VECTOR_SHA, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_ROTATE_LEFT, [](X64Emitter& e, Instr*& i) {
    if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I8, SIG_TYPE_I8C)) {
      Reg8 dest;
      Reg8 src1;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0);
      if (dest != src1) {
        e.mov(dest, src1);
      }
      e.rol(dest, i->src2.value->constant.i8);
      e.EndOp(dest, src1);
    } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I16, SIG_TYPE_I8C)) {
      Reg8 dest;
      Reg16 src1;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0);
      if (dest != src1) {
        e.mov(dest, src1);
      }
      e.rol(dest, i->src2.value->constant.i8);
      e.EndOp(dest, src1);
    } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I32, SIG_TYPE_I8C)) {
      Reg8 dest;
      Reg32 src1;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0);
      if (dest != src1) {
        e.mov(dest, src1);
      }
      e.rol(dest, i->src2.value->constant.i8);
      e.EndOp(dest, src1);
    } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I64, SIG_TYPE_I8C)) {
      Reg8 dest;
      Reg64 src1;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src1, 0);
      if (dest != src1) {
        e.mov(dest, src1);
      }
      e.rol(dest, i->src2.value->constant.i8);
      e.EndOp(dest, src1);
    } else {
      UNIMPLEMENTED_SEQ();
    }
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_BYTE_SWAP, [](X64Emitter& e, Instr*& i) {
    if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16)) {
      Reg16 d, s1;
      e.BeginOp(i->dest, d, REG_DEST | REG_ABCD,
                i->src1.value, s1, 0);
      if (d != s1) {
        e.mov(d, s1);
        e.xchg(d.cvt8(), Reg8(d.getIdx() + 4));
      } else {
        e.xchg(d.cvt8(), Reg8(d.getIdx() + 4));
      }
      e.EndOp(d, s1);
    } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32)) {
      Reg32 d, s1;
      e.BeginOp(i->dest, d, REG_DEST,
                i->src1.value, s1, 0);
      if (d != s1) {
        e.mov(d, s1);
        e.bswap(d);
      } else {
        e.bswap(d);
      }
      e.EndOp(d, s1);
    } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64)) {
      Reg64 d, s1;
      e.BeginOp(i->dest, d, REG_DEST,
                i->src1.value, s1, 0);
      if (d != s1) {
        e.mov(d, s1);
        e.bswap(d);
      } else {
        e.bswap(d);
      }
      e.EndOp(d, s1);
    } else {
      ASSERT_INVALID_TYPE();
    }
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_CNTLZ, [](X64Emitter& e, Instr*& i) {
    if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I8)) {
      Reg8 dest;
      Reg8 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.bsr(dest.cvt16(), src.cvt16());
      // ZF = 1 if zero
      e.mov(e.eax, 16);
      e.cmovz(dest.cvt32(), e.eax);
      e.sub(dest, 8);
      e.xor(dest, 0x7);
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I16)) {
      Reg8 dest;
      Reg16 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.bsr(dest.cvt16(), src);
      // ZF = 1 if zero
      e.mov(e.eax, 16);
      e.cmovz(dest.cvt32(), e.eax);
      e.xor(dest, 0xF);
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I32)) {
      Reg8 dest;
      Reg32 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.bsr(dest.cvt32(), src);
      // ZF = 1 if zero
      e.mov(e.eax, 32);
      e.cmovz(dest.cvt32(), e.eax);
      e.xor(dest, 0x1F);
      e.EndOp(dest, src);
    } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I64)) {
      Reg8 dest;
      Reg64 src;
      e.BeginOp(i->dest, dest, REG_DEST,
                i->src1.value, src, 0);
      e.bsr(dest, src);
      // ZF = 1 if zero
      e.mov(e.eax, 64);
      e.cmovz(dest.cvt32(), e.eax);
      e.xor(dest, 0x3F);
      e.EndOp(dest, src);
    } else {
      UNIMPLEMENTED_SEQ();
    }
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_INSERT, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_EXTRACT, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_SPLAT, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_PERMUTE, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_SWIZZLE, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_PACK, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_UNPACK, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  // --------------------------------------------------------------------------
  // Atomic
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_COMPARE_EXCHANGE, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_ATOMIC_EXCHANGE, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_ATOMIC_ADD, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_ATOMIC_SUB, [](X64Emitter& e, Instr*& i) {
    UNIMPLEMENTED_SEQ();
    i = e.Advance(i);
    return true;
  });
}
