/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Developers. All rights reserved.                      *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */
#ifndef XENIA_CPU_BACKEND_A64_A64_OP_H_
#define XENIA_CPU_BACKEND_A64_A64_OP_H_

#include "xenia/cpu/backend/a64/a64_emitter.h"

#include "xenia/cpu/hir/instr.h"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

// TODO(benvanik): direct usings.
using namespace xe::cpu;
using namespace xe::cpu::hir;
using namespace oaknut;
using namespace oaknut::util;

// Selects the right byte/word/etc from a vector. We need to flip logical
// indices (0,1,2,3,4,5,6,7,...) = (3,2,1,0,7,6,5,4,...)
#define VEC128_B(n) ((n) ^ 0x3)
#define VEC128_W(n) ((n) ^ 0x1)
#define VEC128_D(n) (n)
#define VEC128_F(n) (n)

enum KeyType {
  KEY_TYPE_X = OPCODE_SIG_TYPE_X,
  KEY_TYPE_L = OPCODE_SIG_TYPE_L,
  KEY_TYPE_O = OPCODE_SIG_TYPE_O,
  KEY_TYPE_S = OPCODE_SIG_TYPE_S,
  KEY_TYPE_V_I8 = OPCODE_SIG_TYPE_V + INT8_TYPE,
  KEY_TYPE_V_I16 = OPCODE_SIG_TYPE_V + INT16_TYPE,
  KEY_TYPE_V_I32 = OPCODE_SIG_TYPE_V + INT32_TYPE,
  KEY_TYPE_V_I64 = OPCODE_SIG_TYPE_V + INT64_TYPE,
  KEY_TYPE_V_F32 = OPCODE_SIG_TYPE_V + FLOAT32_TYPE,
  KEY_TYPE_V_F64 = OPCODE_SIG_TYPE_V + FLOAT64_TYPE,
  KEY_TYPE_V_V128 = OPCODE_SIG_TYPE_V + VEC128_TYPE,
};

#pragma pack(push, 1)
union InstrKey {
  uint32_t value;
  struct {
    uint32_t opcode : 8;
    uint32_t dest : 5;
    uint32_t src1 : 5;
    uint32_t src2 : 5;
    uint32_t src3 : 5;
    uint32_t reserved : 4;
  };

  operator uint32_t() const { return value; }

  InstrKey() : value(0) { static_assert_size(*this, sizeof(value)); }
  InstrKey(uint32_t v) : value(v) {}
  InstrKey(const Instr* i) : value(0) {
    opcode = i->opcode->num;
    uint32_t sig = i->opcode->signature;
    dest =
        GET_OPCODE_SIG_TYPE_DEST(sig) ? OPCODE_SIG_TYPE_V + i->dest->type : 0;
    src1 = GET_OPCODE_SIG_TYPE_SRC1(sig);
    if (src1 == OPCODE_SIG_TYPE_V) {
      src1 += i->src1.value->type;
    }
    src2 = GET_OPCODE_SIG_TYPE_SRC2(sig);
    if (src2 == OPCODE_SIG_TYPE_V) {
      src2 += i->src2.value->type;
    }
    src3 = GET_OPCODE_SIG_TYPE_SRC3(sig);
    if (src3 == OPCODE_SIG_TYPE_V) {
      src3 += i->src3.value->type;
    }
  }

  template <Opcode OPCODE, KeyType DEST = KEY_TYPE_X, KeyType SRC1 = KEY_TYPE_X,
            KeyType SRC2 = KEY_TYPE_X, KeyType SRC3 = KEY_TYPE_X>
  struct Construct {
    static const uint32_t value =
        (OPCODE) | (DEST << 8) | (SRC1 << 13) | (SRC2 << 18) | (SRC3 << 23);
  };
};
#pragma pack(pop)
static_assert(sizeof(InstrKey) <= 4, "Key must be 4 bytes");

template <typename... Ts>
struct CombinedStruct;
template <>
struct CombinedStruct<> {};
template <typename T, typename... Ts>
struct CombinedStruct<T, Ts...> : T, CombinedStruct<Ts...> {};

struct OpBase {};

template <typename T, KeyType KEY_TYPE>
struct Op : OpBase {
  static const KeyType key_type = KEY_TYPE;
};

struct VoidOp : Op<VoidOp, KEY_TYPE_X> {
 protected:
  friend struct Op<VoidOp, KEY_TYPE_X>;
  template <hir::Opcode OPCODE, typename... Ts>
  friend struct I;
  void Load(const Instr::Op& op) {}
};

struct OffsetOp : Op<OffsetOp, KEY_TYPE_O> {
  uint64_t value;

 protected:
  friend struct Op<OffsetOp, KEY_TYPE_O>;
  template <hir::Opcode OPCODE, typename... Ts>
  friend struct I;
  void Load(const Instr::Op& op) { this->value = op.offset; }
};

struct SymbolOp : Op<SymbolOp, KEY_TYPE_S> {
  Function* value;

 protected:
  friend struct Op<SymbolOp, KEY_TYPE_S>;
  template <hir::Opcode OPCODE, typename... Ts>
  friend struct I;
  bool Load(const Instr::Op& op) {
    this->value = op.symbol;
    return true;
  }
};

struct LabelOp : Op<LabelOp, KEY_TYPE_L> {
  hir::Label* value;

 protected:
  friend struct Op<LabelOp, KEY_TYPE_L>;
  template <hir::Opcode OPCODE, typename... Ts>
  friend struct I;
  void Load(const Instr::Op& op) { this->value = op.label; }
};

template <typename T, KeyType KEY_TYPE, typename REG_TYPE, typename CONST_TYPE>
struct ValueOp : Op<ValueOp<T, KEY_TYPE, REG_TYPE, CONST_TYPE>, KEY_TYPE> {
  typedef REG_TYPE reg_type;
  const Value* value;
  bool is_constant;
  virtual bool ConstantFitsIn32Reg() const { return true; }
  const REG_TYPE& reg() const {
    assert_true(!is_constant);
    return reg_;
  }
  operator const REG_TYPE&() const { return reg(); }
  bool IsEqual(const T& b) const {
    if (is_constant && b.is_constant) {
      return reinterpret_cast<const T*>(this)->constant() == b.constant();
    } else if (!is_constant && !b.is_constant) {
      return reg_.index() == b.reg_.index();
    } else {
      return false;
    }
  }
  bool IsEqual(const oaknut::Reg& b) const {
    if (is_constant) {
      return false;
    } else if (!is_constant) {
      return reg_.index() == b.index();
    } else {
      return false;
    }
  }
  bool operator==(const T& b) const { return IsEqual(b); }
  bool operator!=(const T& b) const { return !IsEqual(b); }
  bool operator==(const oaknut::Reg& b) const { return IsEqual(b); }
  bool operator!=(const oaknut::Reg& b) const { return !IsEqual(b); }
  void Load(const Instr::Op& op) {
    value = op.value;
    is_constant = value->IsConstant();
    if (!is_constant) {
      A64Emitter::SetupReg(value, reg_);
    }
  }

 protected:
  REG_TYPE reg_ = REG_TYPE(0);
};

struct I8Op : ValueOp<I8Op, KEY_TYPE_V_I8, WReg, int8_t> {
  typedef ValueOp<I8Op, KEY_TYPE_V_I8, WReg, int8_t> BASE;
  const int8_t constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.i8;
  }
};
struct I16Op : ValueOp<I16Op, KEY_TYPE_V_I16, WReg, int16_t> {
  typedef ValueOp<I16Op, KEY_TYPE_V_I16, WReg, int16_t> BASE;
  const int16_t constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.i16;
  }
};
struct I32Op : ValueOp<I32Op, KEY_TYPE_V_I32, WReg, int32_t> {
  typedef ValueOp<I32Op, KEY_TYPE_V_I32, WReg, int32_t> BASE;
  const int32_t constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.i32;
  }
};
struct I64Op : ValueOp<I64Op, KEY_TYPE_V_I64, XReg, int64_t> {
  typedef ValueOp<I64Op, KEY_TYPE_V_I64, XReg, int64_t> BASE;
  const int64_t constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.i64;
  }
  bool ConstantFitsIn32Reg() const override {
    int64_t v = BASE::value->constant.i64;
    if ((v & ~0x7FFFFFFF) == 0) {
      // Fits under 31 bits, so just load using normal mov.
      return true;
    } else if ((v & ~0x7FFFFFFF) == ~0x7FFFFFFF) {
      // Negative number that fits in 32bits.
      return true;
    }
    return false;
  }
};
struct F32Op : ValueOp<F32Op, KEY_TYPE_V_F32, SReg, float> {
  typedef ValueOp<F32Op, KEY_TYPE_V_F32, SReg, float> BASE;
  const float constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.f32;
  }
};
struct F64Op : ValueOp<F64Op, KEY_TYPE_V_F64, DReg, double> {
  typedef ValueOp<F64Op, KEY_TYPE_V_F64, DReg, double> BASE;
  const double constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.f64;
  }
};
struct V128Op : ValueOp<V128Op, KEY_TYPE_V_V128, QReg, vec128_t> {
  typedef ValueOp<V128Op, KEY_TYPE_V_V128, QReg, vec128_t> BASE;
  const vec128_t& constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.v128;
  }
};

template <typename DEST, typename... Tf>
struct DestField;
template <typename DEST>
struct DestField<DEST> {
  DEST dest;

 protected:
  bool LoadDest(const Instr* i) {
    Instr::Op op;
    op.value = i->dest;
    dest.Load(op);
    return true;
  }
};
template <>
struct DestField<VoidOp> {
 protected:
  bool LoadDest(const Instr* i) { return true; }
};

template <hir::Opcode OPCODE, typename... Ts>
struct I;
template <hir::Opcode OPCODE, typename DEST>
struct I<OPCODE, DEST> : DestField<DEST> {
  typedef DestField<DEST> BASE;
  static const hir::Opcode opcode = OPCODE;
  static const uint32_t key =
      InstrKey::Construct<OPCODE, DEST::key_type>::value;
  static const KeyType dest_type = DEST::key_type;
  const Instr* instr;

 protected:
  template <typename SEQ, typename T>
  friend struct Sequence;
  bool Load(const Instr* i) {
    if (InstrKey(i).value == key && BASE::LoadDest(i)) {
      instr = i;
      return true;
    }
    return false;
  }
};
template <hir::Opcode OPCODE, typename DEST, typename SRC1>
struct I<OPCODE, DEST, SRC1> : DestField<DEST> {
  typedef DestField<DEST> BASE;
  static const hir::Opcode opcode = OPCODE;
  static const uint32_t key =
      InstrKey::Construct<OPCODE, DEST::key_type, SRC1::key_type>::value;
  static const KeyType dest_type = DEST::key_type;
  static const KeyType src1_type = SRC1::key_type;
  const Instr* instr;
  SRC1 src1 = {};

 protected:
  template <typename SEQ, typename T>
  friend struct Sequence;
  bool Load(const Instr* i) {
    if (InstrKey(i).value == key && BASE::LoadDest(i)) {
      instr = i;
      src1.Load(i->src1);
      return true;
    }
    return false;
  }
};
template <hir::Opcode OPCODE, typename DEST, typename SRC1, typename SRC2>
struct I<OPCODE, DEST, SRC1, SRC2> : DestField<DEST> {
  typedef DestField<DEST> BASE;
  static const hir::Opcode opcode = OPCODE;
  static const uint32_t key =
      InstrKey::Construct<OPCODE, DEST::key_type, SRC1::key_type,
                          SRC2::key_type>::value;
  static const KeyType dest_type = DEST::key_type;
  static const KeyType src1_type = SRC1::key_type;
  static const KeyType src2_type = SRC2::key_type;
  const Instr* instr;
  SRC1 src1;
  SRC2 src2;

 protected:
  template <typename SEQ, typename T>
  friend struct Sequence;
  bool Load(const Instr* i) {
    if (InstrKey(i).value == key && BASE::LoadDest(i)) {
      instr = i;
      src1.Load(i->src1);
      src2.Load(i->src2);
      return true;
    }
    return false;
  }
};
template <hir::Opcode OPCODE, typename DEST, typename SRC1, typename SRC2,
          typename SRC3>
struct I<OPCODE, DEST, SRC1, SRC2, SRC3> : DestField<DEST> {
  typedef DestField<DEST> BASE;
  static const hir::Opcode opcode = OPCODE;
  static const uint32_t key =
      InstrKey::Construct<OPCODE, DEST::key_type, SRC1::key_type,
                          SRC2::key_type, SRC3::key_type>::value;
  static const KeyType dest_type = DEST::key_type;
  static const KeyType src1_type = SRC1::key_type;
  static const KeyType src2_type = SRC2::key_type;
  static const KeyType src3_type = SRC3::key_type;
  const Instr* instr;
  SRC1 src1;
  SRC2 src2;
  SRC3 src3;

 protected:
  template <typename SEQ, typename T>
  friend struct Sequence;
  bool Load(const Instr* i) {
    if (InstrKey(i).value == key && BASE::LoadDest(i)) {
      instr = i;
      src1.Load(i->src1);
      src2.Load(i->src2);
      src3.Load(i->src3);
      return true;
    }
    return false;
  }
};

template <typename T>
static const T GetTempReg(A64Emitter& e);
template <>
const WReg GetTempReg<WReg>(A64Emitter& e) {
  return W0;
}
template <>
const XReg GetTempReg<XReg>(A64Emitter& e) {
  return X0;
}

template <typename SEQ, typename T>
struct Sequence {
  typedef T EmitArgType;

  static constexpr uint32_t head_key() { return T::key; }

  static bool Select(A64Emitter& e, const Instr* i) {
    T args;
    if (!args.Load(i)) {
      return false;
    }
    SEQ::Emit(e, args);
    return true;
  }

  template <typename REG_FN>
  static void EmitUnaryOp(A64Emitter& e, const EmitArgType& i,
                          const REG_FN& reg_fn) {
    if (i.src1.is_constant) {
      e.MOV(i.dest, i.src1.constant());
      reg_fn(e, i.dest);
    } else {
      if (i.dest != i.src1) {
        e.MOV(i.dest, i.src1);
      }
      reg_fn(e, i.dest);
    }
  }

  template <typename REG_REG_FN, typename REG_CONST_FN>
  static void EmitCommutativeBinaryOp(A64Emitter& e, const EmitArgType& i,
                                      const REG_REG_FN& reg_reg_fn,
                                      const REG_CONST_FN& reg_const_fn) {
    if (i.src1.is_constant) {
      if (i.src2.is_constant) {
        // Both constants.
        if (i.src1.ConstantFitsIn32Reg()) {
          e.MOV(i.dest, i.src2.constant());
          reg_const_fn(e, i.dest, static_cast<int32_t>(i.src1.constant()));
        } else if (i.src2.ConstantFitsIn32Reg()) {
          e.MOV(i.dest, i.src1.constant());
          reg_const_fn(e, i.dest, static_cast<int32_t>(i.src2.constant()));
        } else {
          e.MOV(i.dest, i.src1.constant());
          auto temp = GetTempReg<typename decltype(i.src2)::reg_type>(e);
          e.MOV(temp, i.src2.constant());
          reg_reg_fn(e, i.dest, temp);
        }
      } else {
        // src1 constant.
        if (i.dest == i.src2) {
          if (i.src1.ConstantFitsIn32Reg()) {
            reg_const_fn(e, i.dest, static_cast<int32_t>(i.src1.constant()));
          } else {
            auto temp = GetTempReg<typename decltype(i.src1)::reg_type>(e);
            e.MOV(temp, i.src1.constant());
            reg_reg_fn(e, i.dest, temp);
          }
        } else {
          e.MOV(i.dest, i.src1.constant());
          reg_reg_fn(e, i.dest, i.src2);
        }
      }
    } else if (i.src2.is_constant) {
      if (i.dest == i.src1) {
        if (i.src2.ConstantFitsIn32Reg()) {
          reg_const_fn(e, i.dest, static_cast<int32_t>(i.src2.constant()));
        } else {
          auto temp = GetTempReg<typename decltype(i.src2)::reg_type>(e);
          e.MOV(temp, i.src2.constant());
          reg_reg_fn(e, i.dest, temp);
        }
      } else {
        e.MOV(i.dest, i.src2.constant());
        reg_reg_fn(e, i.dest, i.src1);
      }
    } else {
      if (i.dest == i.src1) {
        reg_reg_fn(e, i.dest, i.src2);
      } else if (i.dest == i.src2) {
        reg_reg_fn(e, i.dest, i.src1);
      } else {
        e.MOV(i.dest, i.src1);
        reg_reg_fn(e, i.dest, i.src2);
      }
    }
  }
  template <typename REG_REG_FN, typename REG_CONST_FN>
  static void EmitAssociativeBinaryOp(A64Emitter& e, const EmitArgType& i,
                                      const REG_REG_FN& reg_reg_fn,
                                      const REG_CONST_FN& reg_const_fn) {
    if (i.src1.is_constant) {
      assert_true(!i.src2.is_constant);
      if (i.dest == i.src2) {
        auto temp = GetTempReg<typename decltype(i.src2)::reg_type>(e);
        e.MOV(temp, i.src2);
        e.MOV(i.dest, i.src1.constant());
        reg_reg_fn(e, i.dest, temp);
      } else {
        e.MOV(i.dest, i.src1.constant());
        reg_reg_fn(e, i.dest, i.src2);
      }
    } else if (i.src2.is_constant) {
      if (i.dest == i.src1) {
        if (i.src2.ConstantFitsIn32Reg()) {
          reg_const_fn(e, i.dest, static_cast<int32_t>(i.src2.constant()));
        } else {
          auto temp = GetTempReg<typename decltype(i.src2)::reg_type>(e);
          e.MOV(temp, i.src2.constant());
          reg_reg_fn(e, i.dest, temp);
        }
      } else {
        e.MOV(i.dest, i.src1);
        if (i.src2.ConstantFitsIn32Reg()) {
          reg_const_fn(e, i.dest, static_cast<int32_t>(i.src2.constant()));
        } else {
          auto temp = GetTempReg<typename decltype(i.src2)::reg_type>(e);
          e.MOV(temp, i.src2.constant());
          reg_reg_fn(e, i.dest, temp);
        }
      }
    } else {
      if (i.dest == i.src1) {
        reg_reg_fn(e, i.dest, i.src2);
      } else if (i.dest == i.src2) {
        auto temp = GetTempReg<typename decltype(i.src2)::reg_type>(e);
        e.MOV(temp, i.src2);
        e.MOV(i.dest, i.src1);
        reg_reg_fn(e, i.dest, temp);
      } else {
        e.MOV(i.dest, i.src1);
        reg_reg_fn(e, i.dest, i.src2);
      }
    }
  }

  template <typename REG = QReg, typename FN>
  static void EmitCommutativeBinaryVOp(A64Emitter& e, const EmitArgType& i,
                                       const FN& fn) {
    if (i.src1.is_constant) {
      assert_true(!i.src2.is_constant);
      e.LoadConstantV(Q0, i.src1.constant());
      fn(e, i.dest, REG(0), i.src2);
    } else if (i.src2.is_constant) {
      assert_true(!i.src1.is_constant);
      e.LoadConstantV(Q0, i.src2.constant());
      fn(e, i.dest, i.src1, REG(0));
    } else {
      fn(e, i.dest, i.src1, i.src2);
    }
  }

  template <typename REG = QReg, typename FN>
  static void EmitAssociativeBinaryVOp(A64Emitter& e, const EmitArgType& i,
                                       const FN& fn) {
    if (i.src1.is_constant) {
      assert_true(!i.src2.is_constant);
      e.LoadConstantV(Q0, i.src1.constant());
      fn(e, i.dest, REG(0), i.src2);
    } else if (i.src2.is_constant) {
      assert_true(!i.src1.is_constant);
      e.LoadConstantV(Q0, i.src2.constant());
      fn(e, i.dest, i.src1, REG(0));
    } else {
      fn(e, i.dest, i.src1, i.src2);
    }
  }

  template <typename REG_REG_FN, typename REG_CONST_FN>
  static void EmitCommutativeCompareOp(A64Emitter& e, const EmitArgType& i,
                                       const REG_REG_FN& reg_reg_fn,
                                       const REG_CONST_FN& reg_const_fn) {
    if (i.src1.is_constant) {
      assert_true(!i.src2.is_constant);
      if (i.src1.ConstantFitsIn32Reg()) {
        reg_const_fn(e, i.src2, static_cast<int32_t>(i.src1.constant()));
      } else {
        auto temp = GetTempReg<typename decltype(i.src1)::reg_type>(e);
        e.MOV(temp, i.src1.constant());
        reg_reg_fn(e, i.src2, temp);
      }
    } else if (i.src2.is_constant) {
      assert_true(!i.src1.is_constant);
      if (i.src2.ConstantFitsIn32Reg()) {
        reg_const_fn(e, i.src1, static_cast<int32_t>(i.src2.constant()));
      } else {
        auto temp = GetTempReg<typename decltype(i.src2)::reg_type>(e);
        e.MOV(temp, i.src2.constant());
        reg_reg_fn(e, i.src1, temp);
      }
    } else {
      reg_reg_fn(e, i.src1, i.src2);
    }
  }
  template <typename REG_REG_FN, typename REG_CONST_FN>
  static void EmitAssociativeCompareOp(A64Emitter& e, const EmitArgType& i,
                                       const REG_REG_FN& reg_reg_fn,
                                       const REG_CONST_FN& reg_const_fn) {
    if (i.src1.is_constant) {
      assert_true(!i.src2.is_constant);
      if (i.src1.ConstantFitsIn32Reg()) {
        reg_const_fn(e, i.dest, i.src2, static_cast<int32_t>(i.src1.constant()),
                     true);
      } else {
        auto temp = GetTempReg<typename decltype(i.src1)::reg_type>(e);
        e.MOV(temp, i.src1.constant());
        reg_reg_fn(e, i.dest, i.src2, temp, true);
      }
    } else if (i.src2.is_constant) {
      assert_true(!i.src1.is_constant);
      if (i.src2.ConstantFitsIn32Reg()) {
        reg_const_fn(e, i.dest, i.src1, static_cast<int32_t>(i.src2.constant()),
                     false);
      } else {
        auto temp = GetTempReg<typename decltype(i.src2)::reg_type>(e);
        e.MOV(temp, i.src2.constant());
        reg_reg_fn(e, i.dest, i.src1, temp, false);
      }
    } else {
      reg_reg_fn(e, i.dest, i.src1, i.src2, false);
    }
  }
};

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_OP_H_
