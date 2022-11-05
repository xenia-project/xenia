/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Xenia Developers. All rights reserved.                      *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */
#ifndef XENIA_CPU_BACKEND_X64_X64_OP_H_
#define XENIA_CPU_BACKEND_X64_X64_OP_H_

#include "xenia/cpu/backend/x64/x64_emitter.h"

#include "xenia/cpu/hir/instr.h"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

// TODO(benvanik): direct usings.
using namespace xe::cpu;
using namespace xe::cpu::hir;
using namespace Xbyak;

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
using InstrKeyValue = uint32_t;
#pragma pack(push, 1)
union InstrKey {
  InstrKeyValue value;
  struct {
    InstrKeyValue opcode : 8;
    InstrKeyValue dest : 5;
    InstrKeyValue src1 : 5;
    InstrKeyValue src2 : 5;
    InstrKeyValue src3 : 5;
    InstrKeyValue reserved : 4;
  };

  operator InstrKeyValue() const { return value; }

  InstrKey() : value(0) { static_assert_size(*this, sizeof(value)); }
  InstrKey(InstrKeyValue v) : value(v) {}

  // this used to take about 1% cpu while precompiling
  // it kept reloading opcode, and also constantly repacking and unpacking the
  // bitfields. instead, we pack the fields at the very end
  InstrKey(const Instr* i) : value(0) {
    const OpcodeInfo* info = i->GetOpcodeInfo();

    InstrKeyValue sig = info->signature;

    OpcodeSignatureType dest_type, src1_type, src2_type, src3_type;

    UnpackOpcodeSig(sig, dest_type, src1_type, src2_type, src3_type);

    InstrKeyValue out_desttype = (InstrKeyValue)dest_type;
    InstrKeyValue out_src1type = (InstrKeyValue)src1_type;
    InstrKeyValue out_src2type = (InstrKeyValue)src2_type;
    InstrKeyValue out_src3type = (InstrKeyValue)src3_type;

    Value* destv = i->dest;
    // pre-deref, even if not value
    Value* src1v = i->src1.value;
    Value* src2v = i->src2.value;
    Value* src3v = i->src3.value;

    if (out_src1type == OPCODE_SIG_TYPE_V) {
      out_src1type += src1v->type;
    }

    if (out_src2type == OPCODE_SIG_TYPE_V) {
      out_src2type += src2v->type;
    }

    if (out_src3type == OPCODE_SIG_TYPE_V) {
      out_src3type += src3v->type;
    }
    opcode = info->num;
    dest = out_desttype ? OPCODE_SIG_TYPE_V + destv->type : 0;
    src1 = out_src1type;
    src2 = out_src2type;
    src3 = out_src3type;
  }

  template <Opcode OPCODE, KeyType DEST = KEY_TYPE_X, KeyType SRC1 = KEY_TYPE_X,
            KeyType SRC2 = KEY_TYPE_X, KeyType SRC3 = KEY_TYPE_X>
  struct Construct {
    static const InstrKeyValue value =
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
      return reg_.getIdx() == b.reg_.getIdx();
    } else {
      return false;
    }
  }
  bool IsEqual(const Xbyak::Reg& b) const {
    if (is_constant) {
      return false;
    } else if (!is_constant) {
      return reg_.getIdx() == b.getIdx();
    } else {
      return false;
    }
  }
  bool operator==(const T& b) const { return IsEqual(b); }
  bool operator!=(const T& b) const { return !IsEqual(b); }
  bool operator==(const Xbyak::Reg& b) const { return IsEqual(b); }
  bool operator!=(const Xbyak::Reg& b) const { return !IsEqual(b); }
  void Load(const Instr::Op& op) {
    value = op.value;
    is_constant = value->IsConstant();
    if (!is_constant) {
      X64Emitter::SetupReg(value, reg_);
    }
  }

 protected:
  REG_TYPE reg_;
};

struct I8Op : ValueOp<I8Op, KEY_TYPE_V_I8, Reg8, int8_t> {
  typedef ValueOp<I8Op, KEY_TYPE_V_I8, Reg8, int8_t> BASE;
  const int8_t constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.i8;
  }
};
struct I16Op : ValueOp<I16Op, KEY_TYPE_V_I16, Reg16, int16_t> {
  typedef ValueOp<I16Op, KEY_TYPE_V_I16, Reg16, int16_t> BASE;
  const int16_t constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.i16;
  }
};
struct I32Op : ValueOp<I32Op, KEY_TYPE_V_I32, Reg32, int32_t> {
  typedef ValueOp<I32Op, KEY_TYPE_V_I32, Reg32, int32_t> BASE;
  const int32_t constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.i32;
  }
};
struct I64Op : ValueOp<I64Op, KEY_TYPE_V_I64, Reg64, int64_t> {
  typedef ValueOp<I64Op, KEY_TYPE_V_I64, Reg64, int64_t> BASE;
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
struct F32Op : ValueOp<F32Op, KEY_TYPE_V_F32, Xmm, float> {
  typedef ValueOp<F32Op, KEY_TYPE_V_F32, Xmm, float> BASE;
  const float constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.f32;
  }
};
struct F64Op : ValueOp<F64Op, KEY_TYPE_V_F64, Xmm, double> {
  typedef ValueOp<F64Op, KEY_TYPE_V_F64, Xmm, double> BASE;
  const double constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.f64;
  }
};
struct V128Op : ValueOp<V128Op, KEY_TYPE_V_V128, Xmm, vec128_t> {
  typedef ValueOp<V128Op, KEY_TYPE_V_V128, Xmm, vec128_t> BASE;
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
  bool Load(const Instr* i, InstrKeyValue kv) {
    if (kv == key && BASE::LoadDest(i)) {
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
  SRC1 src1;

 protected:
  template <typename SEQ, typename T>
  friend struct Sequence;
  bool Load(const Instr* i, InstrKeyValue kv) {
    if (kv == key && BASE::LoadDest(i)) {
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
  bool Load(const Instr* i, InstrKeyValue kv) {
    if (kv == key && BASE::LoadDest(i)) {
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
  bool Load(const Instr* i, InstrKeyValue ikey) {
    if (ikey == key && BASE::LoadDest(i)) {
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
XE_MAYBE_UNUSED
static const T GetTempReg(X64Emitter& e);
template <>
XE_MAYBE_UNUSED const Reg8 GetTempReg<Reg8>(X64Emitter& e) {
  return e.al;
}
template <>
XE_MAYBE_UNUSED const Reg16 GetTempReg<Reg16>(X64Emitter& e) {
  return e.ax;
}
template <>
XE_MAYBE_UNUSED const Reg32 GetTempReg<Reg32>(X64Emitter& e) {
  return e.eax;
}
template <>
XE_MAYBE_UNUSED const Reg64 GetTempReg<Reg64>(X64Emitter& e) {
  return e.rax;
}

template <typename SEQ, typename T>
struct Sequence {
  typedef T EmitArgType;

  static constexpr uint32_t head_key() { return T::key; }

  static bool Select(X64Emitter& e, const Instr* i, InstrKeyValue ikey) {
    T args;
    if (!args.Load(i, ikey)) {
      return false;
    }
    SEQ::Emit(e, args);
    return true;
  }

  template <typename REG_FN>
  static void EmitUnaryOp(X64Emitter& e, const EmitArgType& i,
                          const REG_FN& reg_fn) {
    if (i.src1.is_constant) {
      e.mov(i.dest, i.src1.constant());
      reg_fn(e, i.dest);
    } else {
      if (i.dest != i.src1) {
        e.mov(i.dest, i.src1);
      }
      reg_fn(e, i.dest);
    }
  }

  template <typename REG_REG_FN, typename REG_CONST_FN>
  static void EmitCommutativeBinaryOp(X64Emitter& e, const EmitArgType& i,
                                      const REG_REG_FN& reg_reg_fn,
                                      const REG_CONST_FN& reg_const_fn) {
    if (i.src1.is_constant) {
      if (i.src2.is_constant) {
        // Both constants.
        if (i.src1.ConstantFitsIn32Reg()) {
          e.mov(i.dest, i.src2.constant());
          reg_const_fn(e, i.dest, static_cast<int32_t>(i.src1.constant()));
        } else if (i.src2.ConstantFitsIn32Reg()) {
          e.mov(i.dest, i.src1.constant());
          reg_const_fn(e, i.dest, static_cast<int32_t>(i.src2.constant()));
        } else {
          e.mov(i.dest, i.src1.constant());
          auto temp = GetTempReg<typename decltype(i.src2)::reg_type>(e);
          e.mov(temp, i.src2.constant());
          reg_reg_fn(e, i.dest, temp);
        }
      } else {
        // src1 constant.
        if (i.dest == i.src2) {
          if (i.src1.ConstantFitsIn32Reg()) {
            reg_const_fn(e, i.dest, static_cast<int32_t>(i.src1.constant()));
          } else {
            auto temp = GetTempReg<typename decltype(i.src1)::reg_type>(e);
            e.mov(temp, i.src1.constant());
            reg_reg_fn(e, i.dest, temp);
          }
        } else {
          e.mov(i.dest, i.src1.constant());
          reg_reg_fn(e, i.dest, i.src2);
        }
      }
    } else if (i.src2.is_constant) {
      if (i.dest == i.src1) {
        if (i.src2.ConstantFitsIn32Reg()) {
          reg_const_fn(e, i.dest, static_cast<int32_t>(i.src2.constant()));
        } else {
          auto temp = GetTempReg<typename decltype(i.src2)::reg_type>(e);
          e.mov(temp, i.src2.constant());
          reg_reg_fn(e, i.dest, temp);
        }
      } else {
        e.mov(i.dest, i.src2.constant());
        reg_reg_fn(e, i.dest, i.src1);
      }
    } else {
      if (i.dest == i.src1) {
        reg_reg_fn(e, i.dest, i.src2);
      } else if (i.dest == i.src2) {
        reg_reg_fn(e, i.dest, i.src1);
      } else {
        e.mov(i.dest, i.src1);
        reg_reg_fn(e, i.dest, i.src2);
      }
    }
  }
  template <typename REG_REG_FN, typename REG_CONST_FN>
  static void EmitAssociativeBinaryOp(X64Emitter& e, const EmitArgType& i,
                                      const REG_REG_FN& reg_reg_fn,
                                      const REG_CONST_FN& reg_const_fn) {
    if (i.src1.is_constant) {
      assert_true(!i.src2.is_constant);
      if (i.dest == i.src2) {
        auto temp = GetTempReg<typename decltype(i.src2)::reg_type>(e);
        e.mov(temp, i.src2);
        e.mov(i.dest, i.src1.constant());
        reg_reg_fn(e, i.dest, temp);
      } else {
        e.mov(i.dest, i.src1.constant());
        reg_reg_fn(e, i.dest, i.src2);
      }
    } else if (i.src2.is_constant) {
      if (i.dest == i.src1) {
        if (i.src2.ConstantFitsIn32Reg()) {
          reg_const_fn(e, i.dest, static_cast<int32_t>(i.src2.constant()));
        } else {
          auto temp = GetTempReg<typename decltype(i.src2)::reg_type>(e);
          e.mov(temp, i.src2.constant());
          reg_reg_fn(e, i.dest, temp);
        }
      } else {
        e.mov(i.dest, i.src1);
        if (i.src2.ConstantFitsIn32Reg()) {
          reg_const_fn(e, i.dest, static_cast<int32_t>(i.src2.constant()));
        } else {
          auto temp = GetTempReg<typename decltype(i.src2)::reg_type>(e);
          e.mov(temp, i.src2.constant());
          reg_reg_fn(e, i.dest, temp);
        }
      }
    } else {
      if (i.dest == i.src1) {
        reg_reg_fn(e, i.dest, i.src2);
      } else if (i.dest == i.src2) {
        auto temp = GetTempReg<typename decltype(i.src2)::reg_type>(e);
        e.mov(temp, i.src2);
        e.mov(i.dest, i.src1);
        reg_reg_fn(e, i.dest, temp);
      } else {
        e.mov(i.dest, i.src1);
        reg_reg_fn(e, i.dest, i.src2);
      }
    }
  }

  template <typename FN>
  static void EmitCommutativeBinaryXmmOp(X64Emitter& e, const EmitArgType& i,
                                         const FN& fn) {
    if (i.src1.is_constant) {
      assert_true(!i.src2.is_constant);
      e.LoadConstantXmm(e.xmm0, i.src1.constant());
      fn(e, i.dest, e.xmm0, i.src2);
    } else if (i.src2.is_constant) {
      assert_true(!i.src1.is_constant);
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      fn(e, i.dest, i.src1, e.xmm0);
    } else {
      fn(e, i.dest, i.src1, i.src2);
    }
  }

  template <typename FN>
  static void EmitAssociativeBinaryXmmOp(X64Emitter& e, const EmitArgType& i,
                                         const FN& fn) {
    if (i.src1.is_constant) {
      assert_true(!i.src2.is_constant);
      e.LoadConstantXmm(e.xmm0, i.src1.constant());
      fn(e, i.dest, e.xmm0, i.src2);
    } else if (i.src2.is_constant) {
      assert_true(!i.src1.is_constant);
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      fn(e, i.dest, i.src1, e.xmm0);
    } else {
      fn(e, i.dest, i.src1, i.src2);
    }
  }

  template <typename REG_REG_FN, typename REG_CONST_FN>
  static void EmitCommutativeCompareOp(X64Emitter& e, const EmitArgType& i,
                                       const REG_REG_FN& reg_reg_fn,
                                       const REG_CONST_FN& reg_const_fn) {
    if (i.src1.is_constant) {
      assert_true(!i.src2.is_constant);
      if (i.src1.ConstantFitsIn32Reg()) {
        reg_const_fn(e, i.src2, static_cast<int32_t>(i.src1.constant()));
      } else {
        auto temp = GetTempReg<typename decltype(i.src1)::reg_type>(e);
        e.mov(temp, i.src1.constant());
        reg_reg_fn(e, i.src2, temp);
      }
    } else if (i.src2.is_constant) {
      assert_true(!i.src1.is_constant);
      if (i.src2.ConstantFitsIn32Reg()) {
        reg_const_fn(e, i.src1, static_cast<int32_t>(i.src2.constant()));
      } else {
        auto temp = GetTempReg<typename decltype(i.src2)::reg_type>(e);
        e.mov(temp, i.src2.constant());
        reg_reg_fn(e, i.src1, temp);
      }
    } else {
      reg_reg_fn(e, i.src1, i.src2);
    }
  }
  template <typename REG_REG_FN, typename REG_CONST_FN>
  static void EmitAssociativeCompareOp(X64Emitter& e, const EmitArgType& i,
                                       const REG_REG_FN& reg_reg_fn,
                                       const REG_CONST_FN& reg_const_fn) {
    if (i.src1.is_constant) {
      assert_true(!i.src2.is_constant);
      if (i.src1.ConstantFitsIn32Reg()) {
        reg_const_fn(e, i.dest, i.src2, static_cast<int32_t>(i.src1.constant()),
                     true);
      } else {
        auto temp = GetTempReg<typename decltype(i.src1)::reg_type>(e);
        e.mov(temp, i.src1.constant());
        reg_reg_fn(e, i.dest, i.src2, temp, true);
      }
    } else if (i.src2.is_constant) {
      assert_true(!i.src1.is_constant);
      if (i.src2.ConstantFitsIn32Reg()) {
        reg_const_fn(e, i.dest, i.src1, static_cast<int32_t>(i.src2.constant()),
                     false);
      } else {
        auto temp = GetTempReg<typename decltype(i.src2)::reg_type>(e);
        e.mov(temp, i.src2.constant());
        reg_reg_fn(e, i.dest, i.src1, temp, false);
      }
    } else {
      reg_reg_fn(e, i.dest, i.src1, i.src2, false);
    }
  }
};
template <typename T>
static Xmm GetInputRegOrConstant(X64Emitter& e, const T& input,
                                 Xmm xmm_to_use_if_const) {
  if (input.is_constant) {
    using constant_type = std::remove_reference_t<decltype(input.constant())>;

    if constexpr (std::is_integral_v<constant_type>) {
      vec128_t input_constant = vec128b(0);
      if constexpr (sizeof(constant_type) == 4) {
        input_constant.i32[0] = input.constant();

      } else if constexpr (sizeof(constant_type) == 8) {
        input_constant.low = input.constant();
      } else {
        assert_unhandled_case(sizeof(constant_type));
      }
      e.LoadConstantXmm(xmm_to_use_if_const, input_constant);
    } else {
      e.LoadConstantXmm(xmm_to_use_if_const, input.constant());
    }
    return xmm_to_use_if_const;
  } else {
    return input;
  }
}
}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_X64_X64_OP_H_
