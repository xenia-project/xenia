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
#include <alloy/runtime/symbol_info.h>
#include <alloy/runtime/runtime.h>
#include <alloy/runtime/thread_state.h>

using namespace alloy;
using namespace alloy::backend::x64;
using namespace alloy::backend::x64::lowering;
using namespace alloy::hir;
using namespace alloy::runtime;

using namespace Xbyak;


namespace {

#define UNIMPLEMENTED_SEQ() __debugbreak()
#define ASSERT_INVALID_TYPE() XEASSERTALWAYS()

// TODO(benvanik): emit traces/printfs/etc

void Dummy() {
  //
}

void PrintString(void* raw_context, uint8_t* membase, const char* str) {
  // TODO(benvanik): generate this thunk at runtime? or a shim?
  auto thread_state = *((ThreadState**)raw_context);
  fprintf(stdout, "XE[t] :%d: %s\n", thread_state->GetThreadID(), str);
  fflush(stdout);
}

// TODO(benvanik): fancy stuff.
void CallThunk(void* raw_context, uint8_t* membase,
               FunctionInfo* symbol_info) {
  // TODO(benvanik): generate this thunk at runtime? or a shim?
  auto thread_state = *((ThreadState**)raw_context);

  Function* fn = NULL;
  thread_state->runtime()->ResolveFunction(symbol_info->address(), &fn);
  XEASSERTNOTNULL(fn);
  fn->Call(thread_state);
}
void IssueCall(X64Emitter& e, FunctionInfo* symbol_info, uint32_t flags) {
  e.mov(e.r8, (uint64_t)symbol_info);
  e.mov(e.rax, (uint64_t)CallThunk);
  if (flags & CALL_TAIL) {
    e.jmp(e.rax);
  } else {
    e.call(e.rax);
  }
}

void IndirectCallThunk(void* raw_context, uint8_t* membase,
                       uint64_t target_address) {
  // TODO(benvanik): generate this thunk at runtime? or a shim?
  auto thread_state = *((ThreadState**)raw_context);
  XEASSERTALWAYS();
}
void IssueCallIndirect(X64Emitter& e, Value* target, uint32_t flags) {
  Reg64 r;
  e.BeginOp(target, r, 0);
  if (r != e.r8) {
    e.mov(e.r8, r);
  }
  e.EndOp(r);
  e.mov(e.rax, (uint64_t)IndirectCallThunk);
  if (flags & CALL_TAIL) {
    e.jmp(e.rax);
  } else {
    e.sub(e.rsp, 0x20);
    e.call(e.rax);
    e.add(e.rsp, 0x20);
  }
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

typedef void(v_fn)(X64Emitter& e, Instr& i, const Reg& dest_src);
template<typename T>
void UnaryOpV(X64Emitter& e, Instr*& i, v_fn v_fn,
              T& dest, T& src1) {
  e.BeginOp(i->dest, dest, REG_DEST,
            i->src1.value, src1, 0);
  if (dest == src1) {
    v_fn(e, *i, dest);
  } else {
    e.mov(dest, src1);
    v_fn(e, *i, dest);
  }
  e.EndOp(dest, src1);
}
template<typename CT, typename T>
void UnaryOpC(X64Emitter& e, Instr*& i, v_fn v_fn,
              T& dest, Value* src1) {
  e.BeginOp(i->dest, dest, REG_DEST);
  e.mov(dest, (uint64_t)src1->get_constant(CT()));
  v_fn(e, *i, dest);
  e.EndOp(dest);
}
void UnaryOp(X64Emitter& e, Instr*& i, v_fn v_fn) {
  if (i->Match(SIG_TYPE_I8, SIG_TYPE_I8)) {
    Reg8 dest, src1;
    UnaryOpV(e, i, v_fn, dest, src1);
  } else if (i->Match(SIG_TYPE_I8, SIG_TYPE_I8C)) {
    Reg8 dest;
    UnaryOpC<int8_t>(e, i, v_fn, dest, i->src1.value);
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16)) {
    Reg16 dest, src1;
    UnaryOpV(e, i, v_fn, dest, src1);
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16C)) {
    Reg16 dest;
    UnaryOpC<int16_t>(e, i, v_fn, dest, i->src1.value);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32)) {
    Reg32 dest, src1;
    UnaryOpV(e, i, v_fn, dest, src1);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32C)) {
    Reg32 dest;
    UnaryOpC<int32_t>(e, i, v_fn, dest, i->src1.value);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64)) {
    Reg64 dest, src1;
    UnaryOpV(e, i, v_fn, dest, src1);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64C)) {
    Reg64 dest;
    UnaryOpC<int64_t>(e, i, v_fn, dest, i->src1.value);
  } else {
    ASSERT_INVALID_TYPE();
  }
  if (i->flags & ARITHMETIC_SET_CARRY) {
    // EFLAGS should have CA set?
    // (so long as we don't fuck with it)
    // UNIMPLEMENTED_SEQ();
  }
};

typedef void(vv_fn)(X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src);
typedef void(vc_fn)(X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src);
template<typename TD, typename TS1, typename TS2>
void BinaryOpVV(X64Emitter& e, Instr*& i, vv_fn vv_fn,
                TD& dest, TS1& src1, TS2& src2) {
  e.BeginOp(i->dest, dest, REG_DEST,
            i->src1.value, src1, 0,
            i->src2.value, src2, 0);
  if (dest == src1) {
    vv_fn(e, *i, dest, src2);
  } else if (dest == src2) {
    if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
      vv_fn(e, *i, dest, src1);
    } else {
      // Eww.
      e.mov(e.rax, src1);
      vv_fn(e, *i, e.rax, src2);
      e.mov(dest, e.rax);
    }
  } else {
    e.mov(dest, src1);
    vv_fn(e, *i, dest, src2);
  }
  e.EndOp(dest, src1, src2);
}
template<typename CT, typename TD, typename TS1>
void BinaryOpVC(X64Emitter& e, Instr*& i, vv_fn vv_fn, vc_fn vc_fn,
                TD& dest, TS1& src1, Value* src2) {
  e.BeginOp(i->dest, dest, REG_DEST,
            i->src1.value, src1, 0);
  if (dest.getBit() <= 32) {
    // 32-bit.
    if (dest == src1) {
      vc_fn(e, *i, dest, (uint32_t)src2->get_constant(CT()));
    } else {
      e.mov(dest, src1);
      vc_fn(e, *i, dest, (uint32_t)src2->get_constant(CT()));
    }
  } else {
    // 64-bit.
    if (dest == src1) {
      e.mov(e.rax, src2->constant.i64);
      vv_fn(e, *i, dest, e.rax);
    } else {
      e.mov(e.rax, src2->constant.i64);
      e.mov(dest, src1);
      vv_fn(e, *i, dest, e.rax);
    }
  }
  e.EndOp(dest, src1);
}
template<typename CT, typename TD, typename TS2>
void BinaryOpCV(X64Emitter& e, Instr*& i, vv_fn vv_fn, vc_fn vc_fn,
                TD& dest, Value* src1, TS2& src2) {
  e.BeginOp(i->dest, dest, REG_DEST,
            i->src1.value, src2, 0);
  if (dest.getBit() <= 32) {
    // 32-bit.
    if (dest == src2) {
      if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
        vc_fn(e, *i, dest, (uint32_t)src1->get_constant(CT()));
      } else {
        // Eww.
        e.mov(e.rax, src2);
        e.mov(dest, (uint32_t)src1->get_constant(CT()));
        vv_fn(e, *i, dest, e.rax);
      }
    } else {
      e.mov(dest, src2);
      vc_fn(e, *i, dest, (uint32_t)src1->get_constant(CT()));
    }
  } else {
    // 64-bit.
    if (dest == src2) {
      if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
        e.mov(e.rax, src1->constant.i64);
        vv_fn(e, *i, dest, e.rax);
      } else {
        // Eww.
        e.mov(e.rax, src1->constant.i64);
        vv_fn(e, *i, e.rax, src2);
        e.mov(dest, e.rax);
      }
    } else {
      e.mov(e.rax, src2);
      e.mov(dest, src1->constant.i64);
      vv_fn(e, *i, dest, e.rax);
    }
  }
  e.EndOp(dest, src2);
}
void BinaryOp(X64Emitter& e, Instr*& i, vv_fn vv_fn, vc_fn vc_fn) {
  if (i->Match(SIG_TYPE_I8, SIG_TYPE_I8, SIG_TYPE_I8)) {
    Reg8 dest, src1, src2;
    BinaryOpVV(e, i, vv_fn, dest, src1, src2);
  } else if (i->Match(SIG_TYPE_I8, SIG_TYPE_I8, SIG_TYPE_I8C)) {
    Reg8 dest, src1;
    BinaryOpVC<int8_t>(e, i, vv_fn, vc_fn, dest, src1, i->src2.value);
  } else if (i->Match(SIG_TYPE_I8, SIG_TYPE_I8C, SIG_TYPE_I8)) {
    Reg8 dest, src2;
    BinaryOpCV<int8_t>(e, i, vv_fn, vc_fn, dest, i->src1.value, src2);
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16, SIG_TYPE_I16)) {
    Reg16 dest, src1, src2;
    BinaryOpVV(e, i, vv_fn, dest, src1, src2);
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16, SIG_TYPE_I16C)) {
    Reg16 dest, src1;
    BinaryOpVC<int16_t>(e, i, vv_fn, vc_fn, dest, src1, i->src2.value);
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16C, SIG_TYPE_I16)) {
    Reg16 dest, src2;
    BinaryOpCV<int16_t>(e, i, vv_fn, vc_fn, dest, i->src1.value, src2);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32, SIG_TYPE_I32)) {
    Reg32 dest, src1, src2;
    BinaryOpVV(e, i, vv_fn, dest, src1, src2);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32, SIG_TYPE_I32C)) {
    Reg32 dest, src1;
    BinaryOpVC<int32_t>(e, i, vv_fn, vc_fn, dest, src1, i->src2.value);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32C, SIG_TYPE_I32)) {
    Reg32 dest, src2;
    BinaryOpCV<int32_t>(e, i, vv_fn, vc_fn, dest, i->src1.value, src2);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64, SIG_TYPE_I64)) {
    Reg64 dest, src1, src2;
    BinaryOpVV(e, i, vv_fn, dest, src1, src2);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64, SIG_TYPE_I64C)) {
    Reg64 dest, src1;
    BinaryOpVC<int64_t>(e, i, vv_fn, vc_fn, dest, src1, i->src2.value);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64C, SIG_TYPE_I64)) {
    Reg64 dest, src2;
    BinaryOpCV<int64_t>(e, i, vv_fn, vc_fn, dest, i->src1.value, src2);
  } else {
    ASSERT_INVALID_TYPE();
  }
  if (i->flags & ARITHMETIC_SET_CARRY) {
    // EFLAGS should have CA set?
    // (so long as we don't fuck with it)
    // UNIMPLEMENTED_SEQ();
  }
};

typedef void(vvv_fn)(X64Emitter& e, Instr& i, const Reg& dest_src1, const Operand& src2, const Operand& src3);
typedef void(vvc_fn)(X64Emitter& e, Instr& i, const Reg& dest_src1, const Operand& src2, uint32_t src3);
typedef void(vcv_fn)(X64Emitter& e, Instr& i, const Reg& dest_src1, uint32_t src2, const Operand& src3);
template<typename TD, typename TS1, typename TS2, typename TS3>
void TernaryOpVVV(X64Emitter& e, Instr*& i, vvv_fn vvv_fn,
                  TD& dest, TS1& src1, TS2& src2, TS3& src3) {
  e.BeginOp(i->dest, dest, REG_DEST,
            i->src1.value, src1, 0,
            i->src2.value, src2, 0,
            i->src3.value, src3, 0);
  if (dest == src1) {
    vvv_fn(e, *i, dest, src2, src3);
  } else if (dest == src2) {
    if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
      vvv_fn(e, *i, dest, src1, src3);
    } else {
      UNIMPLEMENTED_SEQ();
    }
  } else {
    e.mov(dest, src1);
    vvv_fn(e, *i, dest, src2, src3);
  }
  e.EndOp(dest, src1, src2, src3);
}
template<typename CT, typename TD, typename TS1, typename TS2>
void TernaryOpVVC(X64Emitter& e, Instr*& i, vvv_fn vvv_fn, vvc_fn vvc_fn,
                  TD& dest, TS1& src1, TS2& src2, Value* src3) {
  e.BeginOp(i->dest, dest, REG_DEST,
            i->src1.value, src1, 0,
            i->src2.value, src2, 0);
  if (dest.getBit() <= 32) {
    // 32-bit.
    if (dest == src1) {
      vvc_fn(e, *i, dest, src2, (uint32_t)src3->get_constant(CT()));
    } else if (dest == src2) {
      if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
        vvc_fn(e, *i, dest, src1, (uint32_t)src3->get_constant(CT()));
      } else {
        // Eww.
        e.mov(e.rax, src2);
        e.mov(dest, src1);
        vvc_fn(e, *i, dest, e.rax, (uint32_t)src3->get_constant(CT()));
      }
    } else {
      e.mov(dest, src1);
      vvc_fn(e, *i, dest, src2, (uint32_t)src3->get_constant(CT()));
    }
  } else {
    // 64-bit.
    if (dest == src1) {
      e.mov(e.rax, src3->constant.i64);
      vvv_fn(e, *i, dest, src2, e.rax);
    } else if (dest == src2) {
      if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
        e.mov(e.rax, src3->constant.i64);
        vvv_fn(e, *i, dest, src1, e.rax);
      } else {
        // Eww.
        e.mov(e.rax, src1);
        e.mov(src1, src2);
        e.mov(dest, e.rax);
        e.mov(e.rax, src3->constant.i64);
        vvv_fn(e, *i, dest, src1, e.rax);
      }
    } else {
      e.mov(e.rax, src3->constant.i64);
      e.mov(dest, src1);
      vvv_fn(e, *i, dest, src2, e.rax);
    }
  }
  e.EndOp(dest, src1);
}

}  // namespace


void alloy::backend::x64::lowering::RegisterSequences(LoweringTable* table) {
  // --------------------------------------------------------------------------
  // General
  // --------------------------------------------------------------------------

  table->AddSequence(OPCODE_COMMENT, [](X64Emitter& e, Instr*& i) {
    // TODO(benvanik): pass through.
    auto str = (const char*)i->src1.offset;
    auto str_copy = xestrdupa(str);
    e.mov(e.r8, (uint64_t)str_copy);
    e.mov(e.rax, (uint64_t)PrintString);
    e.call(e.rax);
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
#if XE_DEBUG
    e.nop();
    e.nop();
    e.mov(e.eax, (uint32_t)i->src1.offset);
    e.nop();
    e.nop();
#endif  // XE_DEBUG

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
    IssueCall(e, i->src1.symbol_info, i->flags);
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_CALL_TRUE, [](X64Emitter& e, Instr*& i) {
    e.inLocalLabel();
    CheckBoolean(e, i->src1.value);
    e.jne(".x", e.T_SHORT);
    IssueCall(e, i->src2.symbol_info, i->flags);
    e.L(".x");
    e.outLocalLabel();
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_CALL_INDIRECT, [](X64Emitter& e, Instr*& i) {
    IssueCallIndirect(e, i->src1.value, i->flags);
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_CALL_INDIRECT_TRUE, [](X64Emitter& e, Instr*& i) {
    e.inLocalLabel();
    CheckBoolean(e, i->src1.value);
    e.jne(".x", e.T_SHORT);
    IssueCallIndirect(e, i->src2.value, i->flags);
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

  table->AddSequence(OPCODE_RETURN_TRUE, [](X64Emitter& e, Instr*& i) {
    e.inLocalLabel();
    CheckBoolean(e, i->src1.value);
    e.jne(".x", e.T_SHORT);
    e.ret();
    e.L(".x");
    e.outLocalLabel();
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
    UnaryOp(
        e, i,
        [](X64Emitter& e, Instr& i, const Reg& dest_src) {
          // nop - the mov will have happened.
        });
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
    Reg8 dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.setc(dest);
    e.EndOp(dest);
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_DID_OVERFLOW, [](X64Emitter& e, Instr*& i) {
    Reg8 dest;
    e.BeginOp(i->dest, dest, REG_DEST);
    e.seto(dest);
    e.EndOp(dest);
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
    BinaryOp(
        e, i,
        [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
          e.add(dest_src, src);
        },
        [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
          e.add(dest_src, src);
        });
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_ADD_CARRY, [](X64Emitter& e, Instr*& i) {
    // dest = src1 + src2 + src3.i8
    if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I8, SIG_TYPE_I8, SIG_TYPE_I8)) {
      Reg8 dest, src1, src2;
      Reg8 ca;
      TernaryOpVVV(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src2, const Operand& src3) {
        e.mov(e.ah, src3);
        e.sahf();
        e.adc(dest_src, src2);
      }, dest, src1, src2, ca);
    } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I16, SIG_TYPE_I16, SIG_TYPE_I8)) {
      Reg16 dest, src1, src2;
      Reg8 ca;
      TernaryOpVVV(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src2, const Operand& src3) {
        e.mov(e.ah, src3);
        e.sahf();
        e.adc(dest_src, src2);
      }, dest, src1, src2, ca);
    } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I32, SIG_TYPE_I32, SIG_TYPE_I8)) {
      Reg32 dest, src1, src2;
      Reg8 ca;
      TernaryOpVVV(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src2, const Operand& src3) {
        e.mov(e.ah, src3);
        e.sahf();
        e.adc(dest_src, src2);
      }, dest, src1, src2, ca);
    } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I64, SIG_TYPE_I64, SIG_TYPE_I8)) {
      Reg64 dest, src1, src2;
      Reg8 ca;
      TernaryOpVVV(e, i, [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src2, const Operand& src3) {
        e.mov(e.ah, src3);
        e.sahf();
        e.adc(dest_src, src2);
      }, dest, src1, src2, ca);
    } else {
      UNIMPLEMENTED_SEQ();
    }
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_SUB, [](X64Emitter& e, Instr*& i) {
    BinaryOp(
        e, i,
        [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
          e.sub(dest_src, src);
        },
        [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
          e.sub(dest_src, src);
        });
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
    BinaryOp(
        e, i,
        [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
          e.and(dest_src, src);
        },
        [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
          e.and(dest_src, src);
        });
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_OR, [](X64Emitter& e, Instr*& i) {
    BinaryOp(
        e, i,
        [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
          e.or(dest_src, src);
        },
        [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
          e.or(dest_src, src);
        });
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_XOR, [](X64Emitter& e, Instr*& i) {
    BinaryOp(
        e, i,
        [](X64Emitter& e, Instr& i, const Reg& dest_src, const Operand& src) {
          e.xor(dest_src, src);
        },
        [](X64Emitter& e, Instr& i, const Reg& dest_src, uint32_t src) {
          e.xor(dest_src, src);
        });
    i = e.Advance(i);
    return true;
  });

  table->AddSequence(OPCODE_NOT, [](X64Emitter& e, Instr*& i) {
    UnaryOp(
        e, i,
        [](X64Emitter& e, Instr& i, const Reg& dest_src) {
          e.not(dest_src);
        });
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
