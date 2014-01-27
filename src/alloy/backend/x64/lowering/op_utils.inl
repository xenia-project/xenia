/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// NOTE: this file is only designed to be included by lowering_sequencies.cc!

#ifndef ALLOY_BACKEND_X64_X64_LOWERING_OP_UTILS_INL_
#define ALLOY_BACKEND_X64_X64_LOWERING_OP_UTILS_INL_

namespace {

// Sets EFLAGs with zf for the given value.
// ZF = 1 if false, 0 = true (so jz = jump if false)
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

// Compares src1 and src2 and calls the given fn to set a byte based on EFLAGS.
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

enum VectoreCompareOp {
  VECTOR_CMP_EQ,
  VECTOR_CMP_GT,
  VECTOR_CMP_GE,
};
// Compares src1 to src2 with the given op and sets the dest.
// Dest will have each part set to all ones if the compare passes.
void VectorCompareXX(X64Emitter& e, Instr*& i, VectoreCompareOp op, bool as_signed) {
  Xmm dest, src1, src2;
  e.BeginOp(i->dest, dest, REG_DEST,
            i->src1.value, src1, 0,
            i->src2.value, src2, 0);
  if (op == VECTOR_CMP_EQ) {
    // Commutative, so simple.
    Xmm real_src;
    if (dest == src1) {
      real_src = src2;
    } else if (dest == src2) {
      real_src = src1;
    } else {
      e.movaps(dest, src1);
      real_src = src2;
    }
    if (i->flags == INT8_TYPE) {
      e.pcmpeqb(dest, real_src);
    } else if (i->flags == INT16_TYPE) {
      e.pcmpeqw(dest, real_src);
    } else if (i->flags == INT32_TYPE) {
      e.pcmpeqd(dest, real_src);
    } else if (i->flags == FLOAT32_TYPE) {
      e.cmpeqps(dest, real_src);
    } else {
      ASSERT_INVALID_TYPE();
    }
  } else if (i->flags == FLOAT32_TYPE) {
    // Float GT/GE must be emulated.
    if (op == VECTOR_CMP_GT) {
      // Have to swap: src2 < src1.
      if (dest == src2) {
        e.cmpltps(dest, src1);
      } else if (dest == src1) {
        e.movaps(e.xmm0, src1);
        e.movaps(dest, src2);
        e.cmpltps(dest, e.xmm0);
      } else {
        e.movaps(dest, src2);
        e.cmpltps(dest, src1);
      }
    } else if (op == VECTOR_CMP_GE) {
      // Have to swap: src2 <= src1.
      if (dest == src2) {
        e.cmpleps(dest, src1);
      } else if (dest == src1) {
        e.movaps(e.xmm0, src1);
        e.movaps(dest, src2);
        e.cmpleps(dest, e.xmm0);
      } else {
        e.movaps(dest, src2);
        e.cmpleps(dest, src1);
      }
    } else {
      ASSERT_INVALID_TYPE();
    }
  } else {
    // Integer types are easier.
    Xmm real_src;
    if (dest == src1) {
      real_src = src2;
    } else if (dest == src2) {
      e.movaps(e.xmm0, src2);
      e.movaps(dest, src1);
      real_src = e.xmm0;
    } else {
      e.movaps(dest, src1);
      real_src = src2;
    }
    if (op == VECTOR_CMP_GT) {
      if (i->flags == INT8_TYPE) {
        if (as_signed) {
          e.pcmpgtb(dest, real_src);
        } else {
          UNIMPLEMENTED_SEQ();
        }
      } else if (i->flags == INT16_TYPE) {
        if (as_signed) {
          e.pcmpgtw(dest, real_src);
        } else {
          UNIMPLEMENTED_SEQ();
        }
      } else if (i->flags == INT32_TYPE) {
        if (as_signed) {
          e.pcmpgtd(dest, real_src);
        } else {
          UNIMPLEMENTED_SEQ();
        }
      } else {
        ASSERT_INVALID_TYPE();
      }
    } else if (op == VECTOR_CMP_GE) {
      if (i->flags == INT8_TYPE) {
        if (as_signed) {
          UNIMPLEMENTED_SEQ();
        } else {
          UNIMPLEMENTED_SEQ();
        }
      } else if (i->flags == INT16_TYPE) {
        if (as_signed) {
          UNIMPLEMENTED_SEQ();
        } else {
          UNIMPLEMENTED_SEQ();
        }
      } else if (i->flags == INT32_TYPE) {
        if (as_signed) {
          UNIMPLEMENTED_SEQ();
        } else {
          UNIMPLEMENTED_SEQ();
        }
      } else {
        ASSERT_INVALID_TYPE();
      }
    } else {
      ASSERT_INVALID_TYPE();
    }
  }
  e.EndOp(dest, src1, src2);
};

typedef void(v_fn)(X64Emitter& e, Instr& i, const Reg& dest_src);
template<typename T>
void IntUnaryOpV(X64Emitter& e, Instr*& i, v_fn v_fn,
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
void IntUnaryOpC(X64Emitter& e, Instr*& i, v_fn v_fn,
              T& dest, Value* src1) {
  e.BeginOp(i->dest, dest, REG_DEST);
  e.mov(dest, (uint64_t)src1->get_constant(CT()));
  v_fn(e, *i, dest);
  e.EndOp(dest);
}
void IntUnaryOp(X64Emitter& e, Instr*& i, v_fn v_fn) {
  if (i->Match(SIG_TYPE_I8, SIG_TYPE_I8)) {
    Reg8 dest, src1;
    IntUnaryOpV(e, i, v_fn, dest, src1);
  } else if (i->Match(SIG_TYPE_I8, SIG_TYPE_I8C)) {
    Reg8 dest;
    IntUnaryOpC<int8_t>(e, i, v_fn, dest, i->src1.value);
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16)) {
    Reg16 dest, src1;
    IntUnaryOpV(e, i, v_fn, dest, src1);
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16C)) {
    Reg16 dest;
    IntUnaryOpC<int16_t>(e, i, v_fn, dest, i->src1.value);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32)) {
    Reg32 dest, src1;
    IntUnaryOpV(e, i, v_fn, dest, src1);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32C)) {
    Reg32 dest;
    IntUnaryOpC<int32_t>(e, i, v_fn, dest, i->src1.value);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64)) {
    Reg64 dest, src1;
    IntUnaryOpV(e, i, v_fn, dest, src1);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64C)) {
    Reg64 dest;
    IntUnaryOpC<int64_t>(e, i, v_fn, dest, i->src1.value);
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
void IntBinaryOpVV(X64Emitter& e, Instr*& i, vv_fn vv_fn,
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
void IntBinaryOpVC(X64Emitter& e, Instr*& i, vv_fn vv_fn, vc_fn vc_fn,
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
void IntBinaryOpCV(X64Emitter& e, Instr*& i, vv_fn vv_fn, vc_fn vc_fn,
                TD& dest, Value* src1, TS2& src2) {
  e.BeginOp(i->dest, dest, REG_DEST,
            i->src2.value, src2, 0);
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
void IntBinaryOp(X64Emitter& e, Instr*& i, vv_fn vv_fn, vc_fn vc_fn) {
  // TODO(benvanik): table lookup. This linear scan is slow.
  // Note: we assume DEST.type = SRC1.type, but that SRC2.type may vary.
  XEASSERT(i->dest->type == i->src1.value->type);
  if (i->Match(SIG_TYPE_I8, SIG_TYPE_I8, SIG_TYPE_I8)) {
    Reg8 dest, src1, src2;
    IntBinaryOpVV(e, i, vv_fn, dest, src1, src2);
  } else if (i->Match(SIG_TYPE_I8, SIG_TYPE_I8, SIG_TYPE_I8C)) {
    Reg8 dest, src1;
    IntBinaryOpVC<int8_t>(e, i, vv_fn, vc_fn, dest, src1, i->src2.value);
  } else if (i->Match(SIG_TYPE_I8, SIG_TYPE_I8C, SIG_TYPE_I8)) {
    Reg8 dest, src2;
    IntBinaryOpCV<int8_t>(e, i, vv_fn, vc_fn, dest, i->src1.value, src2);
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16, SIG_TYPE_I16)) {
    Reg16 dest, src1, src2;
    IntBinaryOpVV(e, i, vv_fn, dest, src1, src2);
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16, SIG_TYPE_I16C)) {
    Reg16 dest, src1;
    IntBinaryOpVC<int16_t>(e, i, vv_fn, vc_fn, dest, src1, i->src2.value);
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16C, SIG_TYPE_I16)) {
    Reg16 dest, src2;
    IntBinaryOpCV<int16_t>(e, i, vv_fn, vc_fn, dest, i->src1.value, src2);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32, SIG_TYPE_I32)) {
    Reg32 dest, src1, src2;
    IntBinaryOpVV(e, i, vv_fn, dest, src1, src2);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32, SIG_TYPE_I32C)) {
    Reg32 dest, src1;
    IntBinaryOpVC<int32_t>(e, i, vv_fn, vc_fn, dest, src1, i->src2.value);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32C, SIG_TYPE_I32)) {
    Reg32 dest, src2;
    IntBinaryOpCV<int32_t>(e, i, vv_fn, vc_fn, dest, i->src1.value, src2);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64, SIG_TYPE_I64)) {
    Reg64 dest, src1, src2;
    IntBinaryOpVV(e, i, vv_fn, dest, src1, src2);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64, SIG_TYPE_I64C)) {
    Reg64 dest, src1;
    IntBinaryOpVC<int64_t>(e, i, vv_fn, vc_fn, dest, src1, i->src2.value);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64C, SIG_TYPE_I64)) {
    Reg64 dest, src2;
    IntBinaryOpCV<int64_t>(e, i, vv_fn, vc_fn, dest, i->src1.value, src2);
  // Start forced src2=i8
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16, SIG_TYPE_I8)) {
    Reg16 dest, src1;
    Reg8 src2;
    IntBinaryOpVV(e, i, vv_fn, dest, src1, src2);
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16, SIG_TYPE_I8C)) {
    Reg16 dest, src1;
    IntBinaryOpVC<int8_t>(e, i, vv_fn, vc_fn, dest, src1, i->src2.value);
  } else if (i->Match(SIG_TYPE_I16, SIG_TYPE_I16C, SIG_TYPE_I8)) {
    Reg16 dest;
    Reg8 src2;
    IntBinaryOpCV<int16_t>(e, i, vv_fn, vc_fn, dest, i->src1.value, src2);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32, SIG_TYPE_I8)) {
    Reg32 dest, src1;
    Reg8 src2;
    IntBinaryOpVV(e, i, vv_fn, dest, src1, src2);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32, SIG_TYPE_I8C)) {
    Reg32 dest, src1;
    IntBinaryOpVC<int8_t>(e, i, vv_fn, vc_fn, dest, src1, i->src2.value);
  } else if (i->Match(SIG_TYPE_I32, SIG_TYPE_I32C, SIG_TYPE_I8)) {
    Reg32 dest;
    Reg8 src2;
    IntBinaryOpCV<int32_t>(e, i, vv_fn, vc_fn, dest, i->src1.value, src2);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64, SIG_TYPE_I8)) {
    Reg64 dest, src1;
    Reg8 src2;
    IntBinaryOpVV(e, i, vv_fn, dest, src1, src2);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64, SIG_TYPE_I8C)) {
    Reg64 dest, src1;
    IntBinaryOpVC<int8_t>(e, i, vv_fn, vc_fn, dest, src1, i->src2.value);
  } else if (i->Match(SIG_TYPE_I64, SIG_TYPE_I64C, SIG_TYPE_I8)) {
    Reg64 dest;
    Reg8 src2;
    IntBinaryOpCV<int64_t>(e, i, vv_fn, vc_fn, dest, i->src1.value, src2);
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
void IntTernaryOpVVV(X64Emitter& e, Instr*& i, vvv_fn vvv_fn,
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
void IntTernaryOpVVC(X64Emitter& e, Instr*& i, vvv_fn vvv_fn, vvc_fn vvc_fn,
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
  e.EndOp(dest, src1, src2);
}
template<typename CT, typename TD, typename TS1, typename TS3>
void IntTernaryOpVCV(X64Emitter& e, Instr*& i, vvv_fn vvv_fn, vcv_fn vcv_fn,
                  TD& dest, TS1& src1, Value* src2, TS3& src3) {
  e.BeginOp(i->dest, dest, REG_DEST,
            i->src1.value, src1, 0,
            i->src3.value, src3, 0);
  if (dest.getBit() <= 32) {
    // 32-bit.
    if (dest == src1) {
      vcv_fn(e, *i, dest, (uint32_t)src2->get_constant(CT()), src3);
    } else if (dest == src3) {
      if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
        vcv_fn(e, *i, dest, (uint32_t)src2->get_constant(CT()), src1);
      } else {
        // Eww.
        e.mov(e.rax, src3);
        e.mov(dest, src1);
        vcv_fn(e, *i, dest, (uint32_t)src2->get_constant(CT()), e.rax);
      }
    } else {
      e.mov(dest, src1);
      vcv_fn(e, *i, dest, (uint32_t)src2->get_constant(CT()), src3);
    }
  } else {
    // 64-bit.
    if (dest == src1) {
      e.mov(e.rax, src2->constant.i64);
      vvv_fn(e, *i, dest, e.rax, src3);
    } else if (dest == src3) {
      if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
        e.mov(e.rax, src2->constant.i64);
        vvv_fn(e, *i, dest, src1, e.rax);
      } else {
        // Eww.
        e.mov(e.rax, src1);
        e.mov(src1, src3);
        e.mov(dest, e.rax);
        e.mov(e.rax, src2->constant.i64);
        vvv_fn(e, *i, dest, e.rax, src1);
      }
    } else {
      e.mov(e.rax, src2->constant.i64);
      e.mov(dest, src1);
      vvv_fn(e, *i, dest, e.rax, src3);
    }
  }
  e.EndOp(dest, src1, src3);
}
void IntTernaryOp(X64Emitter& e, Instr*& i, vvv_fn vvv_fn, vvc_fn vvc_fn, vcv_fn vcv_fn) {
  // TODO(benvanik): table lookup. This linear scan is slow.
  // Note: we assume DEST.type = SRC1.type = SRC2.type, but that SRC3.type may vary.
  XEASSERT(i->dest->type == i->src1.value->type &&
           i->dest->type == i->src2.value->type);
  // TODO(benvanik): table lookup.
  if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I8, SIG_TYPE_I8, SIG_TYPE_I8)) {
    Reg8 dest, src1, src2;
    Reg8 src3;
    IntTernaryOpVVV(e, i, vvv_fn, dest, src1, src2, src3);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I8, SIG_TYPE_I8, SIG_TYPE_I8C)) {
    Reg8 dest, src1, src2;
    IntTernaryOpVVC<int8_t>(e, i, vvv_fn, vvc_fn, dest, src1, src2, i->src3.value);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I16, SIG_TYPE_I16, SIG_TYPE_I8)) {
    Reg16 dest, src1, src2;
    Reg8 src3;
    IntTernaryOpVVV(e, i, vvv_fn, dest, src1, src2, src3);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I16, SIG_TYPE_I16, SIG_TYPE_I8C)) {
    Reg16 dest, src1, src2;
    IntTernaryOpVVC<int8_t>(e, i, vvv_fn, vvc_fn, dest, src1, src2, i->src3.value);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I32, SIG_TYPE_I32, SIG_TYPE_I8)) {
    Reg32 dest, src1, src2;
    Reg8 src3;
    IntTernaryOpVVV(e, i,vvv_fn, dest, src1, src2, src3);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I32, SIG_TYPE_I32, SIG_TYPE_I8C)) {
    Reg32 dest, src1, src2;
    IntTernaryOpVVC<int8_t>(e, i, vvv_fn, vvc_fn, dest, src1, src2, i->src3.value);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I64, SIG_TYPE_I64, SIG_TYPE_I8)) {
    Reg64 dest, src1, src2;
    Reg8 src3;
    IntTernaryOpVVV(e, i, vvv_fn, dest, src1, src2, src3);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I64, SIG_TYPE_I64, SIG_TYPE_I8C)) {
    Reg64 dest, src1, src2;
    IntTernaryOpVVC<int8_t>(e, i, vvv_fn, vvc_fn, dest, src1, src2, i->src3.value);
  //
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I8, SIG_TYPE_I8C, SIG_TYPE_I8)) {
    Reg8 dest, src1, src3;
    IntTernaryOpVCV<int8_t>(e, i, vvv_fn, vcv_fn, dest, src1, i->src2.value, src3);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I16, SIG_TYPE_I16C, SIG_TYPE_I8)) {
    Reg16 dest, src1, src3;
    IntTernaryOpVCV<int16_t>(e, i, vvv_fn, vcv_fn, dest, src1, i->src2.value, src3);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I32, SIG_TYPE_I32C, SIG_TYPE_I8)) {
    Reg32 dest, src1, src3;
    IntTernaryOpVCV<int32_t>(e, i, vvv_fn, vcv_fn, dest, src1, i->src2.value, src3);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I64, SIG_TYPE_I64C, SIG_TYPE_I8)) {
    Reg64 dest, src1, src3;
    IntTernaryOpVCV<int64_t>(e, i, vvv_fn, vcv_fn, dest, src1, i->src2.value, src3);
  } else {
    ASSERT_INVALID_TYPE();
  }
  if (i->flags & ARITHMETIC_SET_CARRY) {
    // EFLAGS should have CA set?
    // (so long as we don't fuck with it)
    // UNIMPLEMENTED_SEQ();
  }
}

}  // namespace

#endif  // ALLOY_BACKEND_X64_X64_LOWERING_OP_UTILS_INL_
