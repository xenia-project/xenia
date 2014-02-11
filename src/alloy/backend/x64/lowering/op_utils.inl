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

#define LIKE_REG(dest, like) Reg(dest.getIdx(), dest.getKind(), like.getBit(), false)
#define TEMP_REG e.r8
#define TEMP_LIKE(like) Reg(TEMP_REG.getIdx(), TEMP_REG.getKind(), like.getBit(), false)

#define STASH_OFFSET 32

// If we are running with tracing on we have to store the EFLAGS in the stack,
// otherwise our calls out to C to print will clear it before DID_CARRY/etc
// can get the value.
#define STORE_EFLAGS 1

void LoadEflags(X64Emitter& e) {
#if STORE_EFLAGS
  e.mov(e.eax, e.dword[e.rsp + STASH_OFFSET]);
  e.push(e.rax);
  e.popf();
#else
  // EFLAGS already present.
#endif  // STORE_EFLAGS
}
void StoreEflags(X64Emitter& e) {
#if STORE_EFLAGS
  e.pushf();
  e.pop(e.qword[e.rsp + STASH_OFFSET]);
#else
  // EFLAGS should have CA set?
  // (so long as we don't fuck with it)
#endif  // STORE_EFLAGS
}

Address Stash(X64Emitter& e, const Xmm& r) {
  // TODO(benvanik): ensure aligned.
  auto addr = e.ptr[e.rsp + STASH_OFFSET];
  e.movups(addr, r);
  return addr;
}

void LoadXmmConstant(X64Emitter& e, const Xmm& dest, const vec128_t& v) {
  if (!v.low && !v.high) {
    // zero
    e.vpxor(dest, dest);
  //} else if (v.low == ~0ull && v.high == ~0ull) {
    // one
    // TODO(benvanik): XMMCONST?
  } else {
    // TODO(benvanik): more efficient loading of partial values?
    e.mov(e.qword[e.rsp + STASH_OFFSET], v.low);
    e.mov(e.qword[e.rsp + STASH_OFFSET + 8], v.high);
    e.vmovaps(dest, e.ptr[e.rsp + STASH_OFFSET]);
  }
}

// Moves a 64bit immediate into memory.
void MovMem64(X64Emitter& e, RegExp& addr, uint64_t v) {
  if ((v & ~0x7FFFFFFF) == 0) {
    // Fits under 31 bits, so just load using normal mov.
    e.mov(e.qword[addr], v);
  } else if ((v & ~0x7FFFFFFF) == ~0x7FFFFFFF) {
    // Negative number that fits in 32bits.
    e.mov(e.qword[addr], v);
  } else {
    // 64bit number that needs double movs.
    e.mov(e.rax, v);
    e.mov(e.qword[addr], e.rax);
  }
}

void CallNative(X64Emitter& e, void* target) {
  e.mov(e.rax, (uint64_t)target);
  e.call(e.rax);
  e.mov(e.rcx, e.qword[e.rsp + StackLayout::GUEST_RCX_HOME]);
  e.mov(e.rdx, e.qword[e.rcx + 8]); // membase
}

void ReloadRDX(X64Emitter& e) {
  e.mov(e.rdx, e.qword[e.rcx + 8]); // membase
}

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
    // TODO(benvanik): mask?
    Xmm src;
    e.BeginOp(v, src, 0);
    e.ptest(src, src);
    e.EndOp(src);
  } else if (v->type == FLOAT64_TYPE) {
    // TODO(benvanik): mask?
    Xmm src;
    e.BeginOp(v, src, 0);
    e.ptest(src, src);
    e.EndOp(src);
  } else if (v->type == VEC128_TYPE) {
    Xmm src;
    e.BeginOp(v, src, 0);
    e.ptest(src, src);
    e.EndOp(src);
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
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_F32, SIG_TYPE_F32)) {
    Reg8 dest;
    Xmm src1, src2;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0,
              i->src2.value, src2, 0);
    e.comiss(src1, src2);
    set_fn(e, dest, false);
    e.EndOp(dest, src1, src2);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_F32, SIG_TYPE_F32C)) {
    Reg8 dest;
    Xmm src1;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0);
    if (i->src2.value->IsConstantZero()) {
      e.pxor(e.xmm0, e.xmm0);
    } else {
      e.mov(e.eax, (uint32_t)i->src2.value->constant.i32);
      e.pinsrd(e.xmm0, e.eax, 0);
    }
    e.comiss(src1, e.xmm0);
    set_fn(e, dest, false);
    e.EndOp(dest, src1);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_F64, SIG_TYPE_F64)) {
    Reg8 dest;
    Xmm src1, src2;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0,
              i->src2.value, src2, 0);
    e.comisd(src1, src2);
    set_fn(e, dest, false);
    e.EndOp(dest, src1, src2);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_F64, SIG_TYPE_F64C)) {
    Reg8 dest;
    Xmm src1;
    e.BeginOp(i->dest, dest, REG_DEST,
              i->src1.value, src1, 0);
    if (i->src2.value->IsConstantZero()) {
      e.pxor(e.xmm0, e.xmm0);
    } else {
      e.mov(e.rax, (uint64_t)i->src2.value->constant.i64);
      e.pinsrq(e.xmm0, e.rax, 0);
    }
    e.comisd(src1, e.xmm0);
    set_fn(e, dest, false);
    e.EndOp(dest, src1);
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
    if (dest.getIdx() == src1.getIdx()) {
      real_src = src2;
    } else if (dest.getIdx() == src2.getIdx()) {
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
      if (dest.getIdx() == src2.getIdx()) {
        e.cmpltps(dest, src1);
      } else if (dest.getIdx() == src1.getIdx()) {
        e.movaps(e.xmm0, src1);
        e.movaps(dest, src2);
        e.cmpltps(dest, e.xmm0);
      } else {
        e.movaps(dest, src2);
        e.cmpltps(dest, src1);
      }
    } else if (op == VECTOR_CMP_GE) {
      // Have to swap: src2 <= src1.
      if (dest.getIdx() == src2.getIdx()) {
        e.cmpleps(dest, src1);
      } else if (dest.getIdx() == src1.getIdx()) {
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
    if (dest.getIdx() == src1.getIdx()) {
      real_src = src2;
    } else if (dest.getIdx() == src2.getIdx()) {
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
  if (dest.getIdx() == src1.getIdx()) {
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
    StoreEflags(e);
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
  if (dest.getIdx() == src1.getIdx()) {
    vv_fn(e, *i, dest, src2);
  } else if (dest.getIdx() == src2.getIdx()) {
    if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
      vv_fn(e, *i, dest, src1);
    } else {
      // Eww.
      auto Ntx = TEMP_LIKE(src1);
      e.mov(Ntx, src1);
      vv_fn(e, *i, Ntx, src2);
      e.mov(dest, Ntx);
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
    if (dest.getIdx() == src1.getIdx()) {
      vc_fn(e, *i, dest, (uint32_t)src2->get_constant(CT()));
    } else {
      e.mov(dest, src1);
      vc_fn(e, *i, dest, (uint32_t)src2->get_constant(CT()));
    }
  } else {
    // 64-bit.
    if (dest.getIdx() == src1.getIdx()) {
      e.mov(TEMP_REG, src2->constant.i64);
      vv_fn(e, *i, dest, TEMP_REG);
    } else {
      e.mov(TEMP_REG, src2->constant.i64);
      e.mov(dest, src1);
      vv_fn(e, *i, dest, TEMP_REG);
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
    if (dest.getIdx() == src2.getIdx()) {
      if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
        vc_fn(e, *i, dest, (uint32_t)src1->get_constant(CT()));
      } else {
        // Eww.
        auto Ntx = TEMP_LIKE(src2);
        e.mov(Ntx, src2);
        e.mov(dest, (uint32_t)src1->get_constant(CT()));
        vv_fn(e, *i, dest, Ntx);
      }
    } else {
      if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
        e.mov(dest, src2);
        vc_fn(e, *i, dest, (uint32_t)src1->get_constant(CT()));
      } else {
        // Need a cv_fn. Or a better way to do all of this.
        e.mov(dest, (uint32_t)src1->get_constant(CT()));
        vv_fn(e, *i, dest, src2);
      }
    }
  } else {
    // 64-bit.
    if (dest.getIdx() == src2.getIdx()) {
      if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
        e.mov(TEMP_REG, src1->constant.i64);
        vv_fn(e, *i, dest, TEMP_REG);
      } else {
        // Eww.
        e.mov(TEMP_REG, src1->constant.i64);
        vv_fn(e, *i, TEMP_REG, src2);
        e.mov(dest, TEMP_REG);
      }
    } else {
      e.mov(TEMP_REG, src2);
      e.mov(dest, src1->constant.i64);
      vv_fn(e, *i, dest, TEMP_REG);
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
    StoreEflags(e);
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
  if (dest.getIdx() == src1.getIdx()) {
    vvv_fn(e, *i, dest, src2, src3);
  } else if (dest.getIdx() == src2.getIdx()) {
    if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
      vvv_fn(e, *i, dest, src1, src3);
    } else {
      UNIMPLEMENTED_SEQ();
    }
  } else if (dest.getIdx() == src3.getIdx()) {
    auto Ntx = TEMP_LIKE(src3);
    e.mov(Ntx, src3);
    e.mov(dest, src1);
    vvv_fn(e, *i, dest, src2, Ntx);
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
    if (dest.getIdx() == src1.getIdx()) {
      vvc_fn(e, *i, dest, src2, (uint32_t)src3->get_constant(CT()));
    } else if (dest == src2) {
      if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
        vvc_fn(e, *i, dest, src1, (uint32_t)src3->get_constant(CT()));
      } else {
        // Eww.
        auto Ntx = TEMP_LIKE(src2);
        e.mov(Ntx, src2);
        e.mov(dest, src1);
        vvc_fn(e, *i, dest, Ntx, (uint32_t)src3->get_constant(CT()));
      }
    } else {
      e.mov(dest, src1);
      vvc_fn(e, *i, dest, src2, (uint32_t)src3->get_constant(CT()));
    }
  } else {
    // 64-bit.
    if (dest.getIdx() == src1.getIdx()) {
      e.mov(TEMP_REG, src3->constant.i64);
      vvv_fn(e, *i, dest, src2, TEMP_REG);
    } else if (dest.getIdx() == src2.getIdx()) {
      if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
        e.mov(TEMP_REG, src3->constant.i64);
        vvv_fn(e, *i, dest, src1, TEMP_REG);
      } else {
        // Eww.
        e.mov(TEMP_REG, src1);
        e.mov(src1, src2);
        e.mov(dest, TEMP_REG);
        e.mov(TEMP_REG, src3->constant.i64);
        vvv_fn(e, *i, dest, src1, TEMP_REG);
      }
    } else {
      e.mov(TEMP_REG, src3->constant.i64);
      e.mov(dest, src1);
      vvv_fn(e, *i, dest, src2, TEMP_REG);
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
    if (dest.getIdx() == src1.getIdx()) {
      vcv_fn(e, *i, dest, (uint32_t)src2->get_constant(CT()), src3);
    } else if (dest.getIdx() == src3.getIdx()) {
      if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
        vcv_fn(e, *i, dest, (uint32_t)src2->get_constant(CT()), src1);
      } else {
        // Eww.
        auto Ntx = TEMP_LIKE(src3);
        e.mov(Ntx, src3);
        e.mov(dest, src1);
        vcv_fn(e, *i, dest, (uint32_t)src2->get_constant(CT()), Ntx);
      }
    } else {
      e.mov(dest, src1);
      vcv_fn(e, *i, dest, (uint32_t)src2->get_constant(CT()), src3);
    }
  } else {
    // 64-bit.
    if (dest.getIdx() == src1.getIdx()) {
      e.mov(TEMP_REG, src2->constant.i64);
      vvv_fn(e, *i, dest, TEMP_REG, src3);
    } else if (dest.getIdx() == src3.getIdx()) {
      if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
        e.mov(TEMP_REG, src2->constant.i64);
        vvv_fn(e, *i, dest, src1, TEMP_REG);
      } else {
        // Eww.
        e.mov(TEMP_REG, src1);
        e.mov(src1, src3);
        e.mov(dest, TEMP_REG);
        e.mov(TEMP_REG, src2->constant.i64);
        vvv_fn(e, *i, dest, TEMP_REG, src1);
      }
    } else {
      e.mov(TEMP_REG, src2->constant.i64);
      e.mov(dest, src1);
      vvv_fn(e, *i, dest, TEMP_REG, src3);
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
    Reg8 dest, src1;
    Reg8 src3;
    IntTernaryOpVCV<int8_t>(e, i, vvv_fn, vcv_fn, dest, src1, i->src2.value, src3);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I16, SIG_TYPE_I16C, SIG_TYPE_I8)) {
    Reg16 dest, src1;
    Reg8 src3;
    IntTernaryOpVCV<int16_t>(e, i, vvv_fn, vcv_fn, dest, src1, i->src2.value, src3);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I32, SIG_TYPE_I32C, SIG_TYPE_I8)) {
    Reg32 dest, src1;
    Reg8 src3;
    IntTernaryOpVCV<int32_t>(e, i, vvv_fn, vcv_fn, dest, src1, i->src2.value, src3);
  } else if (i->Match(SIG_TYPE_IGNORE, SIG_TYPE_I64, SIG_TYPE_I64C, SIG_TYPE_I8)) {
    Reg64 dest, src1;
    Reg8 src3;
    IntTernaryOpVCV<int64_t>(e, i, vvv_fn, vcv_fn, dest, src1, i->src2.value, src3);
  } else {
    ASSERT_INVALID_TYPE();
  }
  if (i->flags & ARITHMETIC_SET_CARRY) {
    StoreEflags(e);
  }
}

// Since alot of SSE ops can take dest + src, just do that.
// Worst case the callee can dedupe.
typedef void(xmm_v_fn)(X64Emitter& e, Instr& i, const Xmm& dest, const Xmm& src);
void XmmUnaryOpV(X64Emitter& e, Instr*& i, xmm_v_fn v_fn,
                 Xmm& dest, Xmm& src1) {
  e.BeginOp(i->dest, dest, REG_DEST,
            i->src1.value, src1, 0);
  v_fn(e, *i, dest, src1);
  e.EndOp(dest, src1);
}
void XmmUnaryOpC(X64Emitter& e, Instr*& i, xmm_v_fn v_fn,
                 Xmm& dest, Value* src1) {
  e.BeginOp(i->dest, dest, REG_DEST);
  if (src1->type == FLOAT32_TYPE) {
    e.mov(e.eax, (uint32_t)src1->constant.i32);
    e.movd(dest, e.eax);
  } else if (src1->type == FLOAT64_TYPE) {
    e.mov(e.rax, (uint64_t)src1->constant.i64);
    e.movq(dest, e.rax);
  } else {
    LoadXmmConstant(e, dest, src1->constant.v128);
  }
  v_fn(e, *i, dest, dest);
  e.EndOp(dest);
}
void XmmUnaryOp(X64Emitter& e, Instr*& i, uint32_t flags, xmm_v_fn v_fn) {
  if (IsFloatType(i->src1.value->type)) {
    if (i->Match(SIG_TYPE_F32, SIG_TYPE_F32)) {
      Xmm dest, src1;
      XmmUnaryOpV(e, i, v_fn, dest, src1);
    } else if (i->Match(SIG_TYPE_F32, SIG_TYPE_F32C)) {
      Xmm dest;
      XmmUnaryOpC(e, i, v_fn, dest, i->src1.value);
    } else if (i->Match(SIG_TYPE_F64, SIG_TYPE_F64)) {
      Xmm dest, src1;
      XmmUnaryOpV(e, i, v_fn, dest, src1);
    } else if (i->Match(SIG_TYPE_F64, SIG_TYPE_F64C)) {
      Xmm dest;
      XmmUnaryOpC(e, i, v_fn, dest, i->src1.value);
    } else {
      ASSERT_INVALID_TYPE();
    }
  } else if (IsVecType(i->src1.value->type)) {
    if (i->Match(SIG_TYPE_V128, SIG_TYPE_V128)) {
      Xmm dest, src1;
      XmmUnaryOpV(e, i, v_fn, dest, src1);
    } else if (i->Match(SIG_TYPE_V128, SIG_TYPE_V128C)) {
      Xmm dest;
      XmmUnaryOpC(e, i, v_fn, dest, i->src1.value);
    } else {
      ASSERT_INVALID_TYPE();
    }
  } else {
    ASSERT_INVALID_TYPE();
  }
};

// TODO(benvanik): allow a vvv form for dest = src1 + src2 that new SSE
//     ops support.
typedef void(xmm_vv_fn)(X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src);
void XmmBinaryOpVV(X64Emitter& e, Instr*& i, xmm_vv_fn vv_fn,
                   Xmm& dest, Xmm& src1, Xmm& src2) {
  e.BeginOp(i->dest, dest, REG_DEST,
            i->src1.value, src1, 0,
            i->src2.value, src2, 0);
  if (dest.getIdx() == src1.getIdx()) {
    vv_fn(e, *i, dest, src2);
  } else if (dest.getIdx() == src2.getIdx()) {
    if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
      vv_fn(e, *i, dest, src1);
    } else {
      // Eww.
      e.movaps(e.xmm0, src1);
      vv_fn(e, *i, e.xmm0, src2);
      e.movaps(dest, e.xmm0);
    }
  } else {
    e.movaps(dest, src1);
    vv_fn(e, *i, dest, src2);
  }
  e.EndOp(dest, src1, src2);
}
void XmmBinaryOpVC(X64Emitter& e, Instr*& i, xmm_vv_fn vv_fn,
                   Xmm& dest, Xmm& src1, Value* src2) {
  e.BeginOp(i->dest, dest, REG_DEST,
            i->src1.value, src1, 0);
  if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
    if (src2->type == FLOAT32_TYPE) {
      e.mov(e.eax, (uint32_t)src2->constant.i32);
      e.movss(dest, e.eax);
    } else if (src2->type == FLOAT64_TYPE) {
      e.mov(e.rax, (uint64_t)src2->constant.i64);
      e.movsd(dest, e.rax);
    } else {
      LoadXmmConstant(e, dest, src2->constant.v128);
    }
    vv_fn(e, *i, dest, src1);
  } else {
    if (dest.getIdx() != src1.getIdx()) {
      e.movaps(dest, src1);
    }
    if (src2->type == FLOAT32_TYPE) {
      e.mov(e.eax, (uint32_t)src2->constant.i32);
      e.movss(e.xmm0, e.eax);
    } else if (src2->type == FLOAT64_TYPE) {
      e.mov(e.rax, (uint64_t)src2->constant.i64);
      e.movsd(e.xmm0, e.rax);
    } else {
      LoadXmmConstant(e, e.xmm0, src2->constant.v128);
    }
    vv_fn(e, *i, dest, e.xmm0);
  }
  e.EndOp(dest, src1);
}
void XmmBinaryOpCV(X64Emitter& e, Instr*& i, xmm_vv_fn vv_fn,
                   Xmm& dest, Value* src1, Xmm& src2) {
  e.BeginOp(i->dest, dest, REG_DEST,
            i->src2.value, src2, 0);
  if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
    if (src1->type == FLOAT32_TYPE) {
      e.mov(e.eax, (uint32_t)src1->constant.i32);
      e.movss(dest, e.eax);
    } else if (src1->type == FLOAT64_TYPE) {
      e.mov(e.rax, (uint64_t)src1->constant.i64);
      e.movsd(dest, e.rax);
    } else {
      LoadXmmConstant(e, dest, src1->constant.v128);
    }
    vv_fn(e, *i, dest, src2);
  } else {
    auto real_src2 = src2;
    if (dest.getIdx() == src2.getIdx()) {
      e.movaps(e.xmm0, src2);
      real_src2 = e.xmm0;
    }
    if (src1->type == FLOAT32_TYPE) {
      e.mov(e.eax, (uint32_t)src1->constant.i32);
      e.movss(dest, e.eax);
    } else if (src1->type == FLOAT64_TYPE) {
      e.mov(e.rax, (uint64_t)src1->constant.i64);
      e.movsd(dest, e.rax);
    } else {
      LoadXmmConstant(e, dest, src1->constant.v128);
    }
    vv_fn(e, *i, dest, real_src2);
  }
  e.EndOp(dest, src2);
}
void XmmBinaryOp(X64Emitter& e, Instr*& i, uint32_t flags, xmm_vv_fn vv_fn) {
  // TODO(benvanik): table lookup. This linear scan is slow.
  if (!i->src1.value->IsConstant() && !i->src2.value->IsConstant()) {
    Xmm dest, src1, src2;
    XmmBinaryOpVV(e, i, vv_fn, dest, src1, src2);
  } else if (!i->src1.value->IsConstant() && i->src2.value->IsConstant()) {
    Xmm dest, src1;
    XmmBinaryOpVC(e, i, vv_fn, dest, src1, i->src2.value);
  } else if (i->src1.value->IsConstant() && !i->src2.value->IsConstant()) {
    Xmm dest, src2;
    XmmBinaryOpCV(e, i, vv_fn, dest, i->src1.value, src2);
  } else {
    ASSERT_INVALID_TYPE();
  }
  if (flags & ARITHMETIC_SET_CARRY) {
    StoreEflags(e);
  }
};

typedef void(xmm_vvv_fn)(X64Emitter& e, Instr& i, const Xmm& dest_src, const Xmm& src2, const Xmm& src3);
void XmmTernaryOpVVV(X64Emitter& e, Instr*& i, xmm_vvv_fn vvv_fn,
                     Xmm& dest, Xmm& src1, Xmm& src2, Xmm& src3) {
  e.BeginOp(i->dest, dest, REG_DEST,
            i->src1.value, src1, 0,
            i->src2.value, src2, 0,
            i->src3.value, src3, 0);
  if (dest.getIdx() == src1.getIdx()) {
    vvv_fn(e, *i, dest, src2, src3);
  } else if (dest.getIdx() == src2.getIdx()) {
    if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
      vvv_fn(e, *i, dest, src1, src3);
    } else {
      // Eww.
      e.movaps(e.xmm0, src1);
      vvv_fn(e, *i, e.xmm0, src2, src3);
      e.movaps(dest, e.xmm0);
    }
  } else if (dest.getIdx() == src3.getIdx()) {
    if (i->opcode->flags & OPCODE_FLAG_COMMUNATIVE) {
      vvv_fn(e, *i, dest, src1, src2);
    } else {
      e.movaps(e.xmm0, src3);
      e.movaps(dest, src1);
      vvv_fn(e, *i, dest, src2, e.xmm0);
    }
  } else {
    e.movaps(dest, src1);
    vvv_fn(e, *i, dest, src2, src3);
  }
  e.EndOp(dest, src1, src2, src3);
}
void XmmTernaryOp(X64Emitter& e, Instr*& i, uint32_t flags, xmm_vvv_fn vvv_fn) {
  // TODO(benvanik): table lookup. This linear scan is slow.
  if (!i->src1.value->IsConstant() && !i->src2.value->IsConstant() &&
      !i->src3.value->IsConstant()) {
    Xmm dest, src1, src2, src3;
    XmmTernaryOpVVV(e, i, vvv_fn, dest, src1, src2, src3);
  } else {
    ASSERT_INVALID_TYPE();
  }
  if (flags & ARITHMETIC_SET_CARRY) {
    StoreEflags(e);
  }
};

}  // namespace

#endif  // ALLOY_BACKEND_X64_X64_LOWERING_OP_UTILS_INL_
