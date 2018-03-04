/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// A note about vectors:
// Xenia represents vectors as xyzw pairs, with indices 0123.
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

#include "xenia/cpu/backend/x64/x64_sequences.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "xenia/base/assert.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/backend/x64/x64_emitter.h"
#include "xenia/cpu/backend/x64/x64_tracers.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/processor.h"

// For OPCODE_PACK/OPCODE_UNPACK
#include "third_party/half/include/half.hpp"

namespace xe {
namespace cpu {
namespace backend {
namespace x64 {

using namespace Xbyak;

// TODO(benvanik): direct usings.
using namespace xe::cpu;
using namespace xe::cpu::hir;

using xe::cpu::hir::Instr;

typedef bool (*SequenceSelectFn)(X64Emitter&, const Instr*);
std::unordered_map<uint32_t, SequenceSelectFn> sequence_table;

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
  struct {
    uint32_t opcode : 8;
    uint32_t dest : 5;
    uint32_t src1 : 5;
    uint32_t src2 : 5;
    uint32_t src3 : 5;
    uint32_t reserved : 4;
  };
  uint32_t value;

  operator uint32_t() const { return value; }

  InstrKey() : value(0) {}
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
  template <typename T, KeyType KEY_TYPE>
  friend struct Op;
  template <hir::Opcode OPCODE, typename... Ts>
  friend struct I;
  void Load(const Instr::Op& op) {}
};

struct OffsetOp : Op<OffsetOp, KEY_TYPE_O> {
  uint64_t value;

 protected:
  template <typename T, KeyType KEY_TYPE>
  friend struct Op;
  template <hir::Opcode OPCODE, typename... Ts>
  friend struct I;
  void Load(const Instr::Op& op) { this->value = op.offset; }
};

struct SymbolOp : Op<SymbolOp, KEY_TYPE_S> {
  Function* value;

 protected:
  template <typename T, KeyType KEY_TYPE>
  friend struct Op;
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
  template <typename T, KeyType KEY_TYPE>
  friend struct Op;
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
  SRC1 src1;

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
const T GetTempReg(X64Emitter& e);
template <>
const Reg8 GetTempReg<Reg8>(X64Emitter& e) {
  return e.al;
}
template <>
const Reg16 GetTempReg<Reg16>(X64Emitter& e) {
  return e.ax;
}
template <>
const Reg32 GetTempReg<Reg32>(X64Emitter& e) {
  return e.eax;
}
template <>
const Reg64 GetTempReg<Reg64>(X64Emitter& e) {
  return e.rax;
}

template <typename SEQ, typename T>
struct Sequence {
  typedef T EmitArgType;

  static constexpr uint32_t head_key() { return T::key; }

  static bool Select(X64Emitter& e, const Instr* i) {
    T args;
    if (!args.Load(i)) {
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
void Register() {
  sequence_table.insert({T::head_key(), T::Select});
}
template <typename T, typename Tn, typename... Ts>
void Register() {
  Register<T>();
  Register<Tn, Ts...>();
}
#define EMITTER_OPCODE_TABLE(name, ...) \
  void Register_##name() { Register<__VA_ARGS__>(); }

// ============================================================================
// OPCODE_COMMENT
// ============================================================================
struct COMMENT : Sequence<COMMENT, I<OPCODE_COMMENT, VoidOp, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (IsTracingInstr()) {
      auto str = reinterpret_cast<const char*>(i.src1.value);
      // TODO(benvanik): pass through.
      // TODO(benvanik): don't just leak this memory.
      auto str_copy = strdup(str);
      e.mov(e.rdx, reinterpret_cast<uint64_t>(str_copy));
      e.CallNative(reinterpret_cast<void*>(TraceString));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_COMMENT, COMMENT);

// ============================================================================
// OPCODE_NOP
// ============================================================================
struct NOP : Sequence<NOP, I<OPCODE_NOP, VoidOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) { e.nop(); }
};
EMITTER_OPCODE_TABLE(OPCODE_NOP, NOP);

// ============================================================================
// OPCODE_SOURCE_OFFSET
// ============================================================================
struct SOURCE_OFFSET
    : Sequence<SOURCE_OFFSET, I<OPCODE_SOURCE_OFFSET, VoidOp, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.MarkSourceOffset(i.instr);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SOURCE_OFFSET, SOURCE_OFFSET);

// ============================================================================
// OPCODE_DEBUG_BREAK
// ============================================================================
struct DEBUG_BREAK : Sequence<DEBUG_BREAK, I<OPCODE_DEBUG_BREAK, VoidOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) { e.DebugBreak(); }
};
EMITTER_OPCODE_TABLE(OPCODE_DEBUG_BREAK, DEBUG_BREAK);

// ============================================================================
// OPCODE_DEBUG_BREAK_TRUE
// ============================================================================
struct DEBUG_BREAK_TRUE_I8
    : Sequence<DEBUG_BREAK_TRUE_I8, I<OPCODE_DEBUG_BREAK_TRUE, VoidOp, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.DebugBreak();
    e.L(skip);
  }
};
struct DEBUG_BREAK_TRUE_I16
    : Sequence<DEBUG_BREAK_TRUE_I16,
               I<OPCODE_DEBUG_BREAK_TRUE, VoidOp, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.DebugBreak();
    e.L(skip);
  }
};
struct DEBUG_BREAK_TRUE_I32
    : Sequence<DEBUG_BREAK_TRUE_I32,
               I<OPCODE_DEBUG_BREAK_TRUE, VoidOp, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.DebugBreak();
    e.L(skip);
  }
};
struct DEBUG_BREAK_TRUE_I64
    : Sequence<DEBUG_BREAK_TRUE_I64,
               I<OPCODE_DEBUG_BREAK_TRUE, VoidOp, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.DebugBreak();
    e.L(skip);
  }
};
struct DEBUG_BREAK_TRUE_F32
    : Sequence<DEBUG_BREAK_TRUE_F32,
               I<OPCODE_DEBUG_BREAK_TRUE, VoidOp, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.DebugBreak();
    e.L(skip);
  }
};
struct DEBUG_BREAK_TRUE_F64
    : Sequence<DEBUG_BREAK_TRUE_F64,
               I<OPCODE_DEBUG_BREAK_TRUE, VoidOp, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.DebugBreak();
    e.L(skip);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DEBUG_BREAK_TRUE, DEBUG_BREAK_TRUE_I8,
                     DEBUG_BREAK_TRUE_I16, DEBUG_BREAK_TRUE_I32,
                     DEBUG_BREAK_TRUE_I64, DEBUG_BREAK_TRUE_F32,
                     DEBUG_BREAK_TRUE_F64);

// ============================================================================
// OPCODE_TRAP
// ============================================================================
struct TRAP : Sequence<TRAP, I<OPCODE_TRAP, VoidOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.Trap(i.instr->flags);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_TRAP, TRAP);

// ============================================================================
// OPCODE_TRAP_TRUE
// ============================================================================
struct TRAP_TRUE_I8
    : Sequence<TRAP_TRUE_I8, I<OPCODE_TRAP_TRUE, VoidOp, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Trap(i.instr->flags);
    e.L(skip);
  }
};
struct TRAP_TRUE_I16
    : Sequence<TRAP_TRUE_I16, I<OPCODE_TRAP_TRUE, VoidOp, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Trap(i.instr->flags);
    e.L(skip);
  }
};
struct TRAP_TRUE_I32
    : Sequence<TRAP_TRUE_I32, I<OPCODE_TRAP_TRUE, VoidOp, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Trap(i.instr->flags);
    e.L(skip);
  }
};
struct TRAP_TRUE_I64
    : Sequence<TRAP_TRUE_I64, I<OPCODE_TRAP_TRUE, VoidOp, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Trap(i.instr->flags);
    e.L(skip);
  }
};
struct TRAP_TRUE_F32
    : Sequence<TRAP_TRUE_F32, I<OPCODE_TRAP_TRUE, VoidOp, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Trap(i.instr->flags);
    e.L(skip);
  }
};
struct TRAP_TRUE_F64
    : Sequence<TRAP_TRUE_F64, I<OPCODE_TRAP_TRUE, VoidOp, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Trap(i.instr->flags);
    e.L(skip);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_TRAP_TRUE, TRAP_TRUE_I8, TRAP_TRUE_I16,
                     TRAP_TRUE_I32, TRAP_TRUE_I64, TRAP_TRUE_F32,
                     TRAP_TRUE_F64);

// ============================================================================
// OPCODE_CALL
// ============================================================================
struct CALL : Sequence<CALL, I<OPCODE_CALL, VoidOp, SymbolOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src1.value->is_guest());
    e.Call(i.instr, static_cast<GuestFunction*>(i.src1.value));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CALL, CALL);

// ============================================================================
// OPCODE_CALL_TRUE
// ============================================================================
struct CALL_TRUE_I8
    : Sequence<CALL_TRUE_I8, I<OPCODE_CALL_TRUE, VoidOp, I8Op, SymbolOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->is_guest());
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Call(i.instr, static_cast<GuestFunction*>(i.src2.value));
    e.L(skip);
  }
};
struct CALL_TRUE_I16
    : Sequence<CALL_TRUE_I16, I<OPCODE_CALL_TRUE, VoidOp, I16Op, SymbolOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->is_guest());
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Call(i.instr, static_cast<GuestFunction*>(i.src2.value));
    e.L(skip);
  }
};
struct CALL_TRUE_I32
    : Sequence<CALL_TRUE_I32, I<OPCODE_CALL_TRUE, VoidOp, I32Op, SymbolOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->is_guest());
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Call(i.instr, static_cast<GuestFunction*>(i.src2.value));
    e.L(skip);
  }
};
struct CALL_TRUE_I64
    : Sequence<CALL_TRUE_I64, I<OPCODE_CALL_TRUE, VoidOp, I64Op, SymbolOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->is_guest());
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Call(i.instr, static_cast<GuestFunction*>(i.src2.value));
    e.L(skip);
  }
};
struct CALL_TRUE_F32
    : Sequence<CALL_TRUE_F32, I<OPCODE_CALL_TRUE, VoidOp, F32Op, SymbolOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->is_guest());
    e.vptest(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Call(i.instr, static_cast<GuestFunction*>(i.src2.value));
    e.L(skip);
  }
};
struct CALL_TRUE_F64
    : Sequence<CALL_TRUE_F64, I<OPCODE_CALL_TRUE, VoidOp, F64Op, SymbolOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->is_guest());
    e.vptest(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip);
    e.Call(i.instr, static_cast<GuestFunction*>(i.src2.value));
    e.L(skip);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CALL_TRUE, CALL_TRUE_I8, CALL_TRUE_I16,
                     CALL_TRUE_I32, CALL_TRUE_I64, CALL_TRUE_F32,
                     CALL_TRUE_F64);

// ============================================================================
// OPCODE_CALL_INDIRECT
// ============================================================================
struct CALL_INDIRECT
    : Sequence<CALL_INDIRECT, I<OPCODE_CALL_INDIRECT, VoidOp, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.CallIndirect(i.instr, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CALL_INDIRECT, CALL_INDIRECT);

// ============================================================================
// OPCODE_CALL_INDIRECT_TRUE
// ============================================================================
struct CALL_INDIRECT_TRUE_I8
    : Sequence<CALL_INDIRECT_TRUE_I8,
               I<OPCODE_CALL_INDIRECT_TRUE, VoidOp, I8Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip, CodeGenerator::T_NEAR);
    e.CallIndirect(i.instr, i.src2);
    e.L(skip);
  }
};
struct CALL_INDIRECT_TRUE_I16
    : Sequence<CALL_INDIRECT_TRUE_I16,
               I<OPCODE_CALL_INDIRECT_TRUE, VoidOp, I16Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip, CodeGenerator::T_NEAR);
    e.CallIndirect(i.instr, i.src2);
    e.L(skip);
  }
};
struct CALL_INDIRECT_TRUE_I32
    : Sequence<CALL_INDIRECT_TRUE_I32,
               I<OPCODE_CALL_INDIRECT_TRUE, VoidOp, I32Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip, CodeGenerator::T_NEAR);
    e.CallIndirect(i.instr, i.src2);
    e.L(skip);
  }
};
struct CALL_INDIRECT_TRUE_I64
    : Sequence<CALL_INDIRECT_TRUE_I64,
               I<OPCODE_CALL_INDIRECT_TRUE, VoidOp, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip, CodeGenerator::T_NEAR);
    e.CallIndirect(i.instr, i.src2);
    e.L(skip);
  }
};
struct CALL_INDIRECT_TRUE_F32
    : Sequence<CALL_INDIRECT_TRUE_F32,
               I<OPCODE_CALL_INDIRECT_TRUE, VoidOp, F32Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip, CodeGenerator::T_NEAR);
    e.CallIndirect(i.instr, i.src2);
    e.L(skip);
  }
};
struct CALL_INDIRECT_TRUE_F64
    : Sequence<CALL_INDIRECT_TRUE_F64,
               I<OPCODE_CALL_INDIRECT_TRUE, VoidOp, F64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    Xbyak::Label skip;
    e.jz(skip, CodeGenerator::T_NEAR);
    e.CallIndirect(i.instr, i.src2);
    e.L(skip);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CALL_INDIRECT_TRUE, CALL_INDIRECT_TRUE_I8,
                     CALL_INDIRECT_TRUE_I16, CALL_INDIRECT_TRUE_I32,
                     CALL_INDIRECT_TRUE_I64, CALL_INDIRECT_TRUE_F32,
                     CALL_INDIRECT_TRUE_F64);

// ============================================================================
// OPCODE_CALL_EXTERN
// ============================================================================
struct CALL_EXTERN
    : Sequence<CALL_EXTERN, I<OPCODE_CALL_EXTERN, VoidOp, SymbolOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.CallExtern(i.instr, i.src1.value);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CALL_EXTERN, CALL_EXTERN);

// ============================================================================
// OPCODE_RETURN
// ============================================================================
struct RETURN : Sequence<RETURN, I<OPCODE_RETURN, VoidOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // If this is the last instruction in the last block, just let us
    // fall through.
    if (i.instr->next || i.instr->block->next) {
      e.jmp(e.epilog_label(), CodeGenerator::T_NEAR);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_RETURN, RETURN);

// ============================================================================
// OPCODE_RETURN_TRUE
// ============================================================================
struct RETURN_TRUE_I8
    : Sequence<RETURN_TRUE_I8, I<OPCODE_RETURN_TRUE, VoidOp, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jnz(e.epilog_label(), CodeGenerator::T_NEAR);
  }
};
struct RETURN_TRUE_I16
    : Sequence<RETURN_TRUE_I16, I<OPCODE_RETURN_TRUE, VoidOp, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jnz(e.epilog_label(), CodeGenerator::T_NEAR);
  }
};
struct RETURN_TRUE_I32
    : Sequence<RETURN_TRUE_I32, I<OPCODE_RETURN_TRUE, VoidOp, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jnz(e.epilog_label(), CodeGenerator::T_NEAR);
  }
};
struct RETURN_TRUE_I64
    : Sequence<RETURN_TRUE_I64, I<OPCODE_RETURN_TRUE, VoidOp, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jnz(e.epilog_label(), CodeGenerator::T_NEAR);
  }
};
struct RETURN_TRUE_F32
    : Sequence<RETURN_TRUE_F32, I<OPCODE_RETURN_TRUE, VoidOp, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.jnz(e.epilog_label(), CodeGenerator::T_NEAR);
  }
};
struct RETURN_TRUE_F64
    : Sequence<RETURN_TRUE_F64, I<OPCODE_RETURN_TRUE, VoidOp, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.jnz(e.epilog_label(), CodeGenerator::T_NEAR);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_RETURN_TRUE, RETURN_TRUE_I8, RETURN_TRUE_I16,
                     RETURN_TRUE_I32, RETURN_TRUE_I64, RETURN_TRUE_F32,
                     RETURN_TRUE_F64);

// ============================================================================
// OPCODE_SET_RETURN_ADDRESS
// ============================================================================
struct SET_RETURN_ADDRESS
    : Sequence<SET_RETURN_ADDRESS,
               I<OPCODE_SET_RETURN_ADDRESS, VoidOp, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.SetReturnAddress(i.src1.constant());
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SET_RETURN_ADDRESS, SET_RETURN_ADDRESS);

// ============================================================================
// OPCODE_BRANCH
// ============================================================================
struct BRANCH : Sequence<BRANCH, I<OPCODE_BRANCH, VoidOp, LabelOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.jmp(i.src1.value->name, e.T_NEAR);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_BRANCH, BRANCH);

// ============================================================================
// OPCODE_BRANCH_TRUE
// ============================================================================
struct BRANCH_TRUE_I8
    : Sequence<BRANCH_TRUE_I8, I<OPCODE_BRANCH_TRUE, VoidOp, I8Op, LabelOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jnz(i.src2.value->name, e.T_NEAR);
  }
};
struct BRANCH_TRUE_I16
    : Sequence<BRANCH_TRUE_I16, I<OPCODE_BRANCH_TRUE, VoidOp, I16Op, LabelOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jnz(i.src2.value->name, e.T_NEAR);
  }
};
struct BRANCH_TRUE_I32
    : Sequence<BRANCH_TRUE_I32, I<OPCODE_BRANCH_TRUE, VoidOp, I32Op, LabelOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jnz(i.src2.value->name, e.T_NEAR);
  }
};
struct BRANCH_TRUE_I64
    : Sequence<BRANCH_TRUE_I64, I<OPCODE_BRANCH_TRUE, VoidOp, I64Op, LabelOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jnz(i.src2.value->name, e.T_NEAR);
  }
};
struct BRANCH_TRUE_F32
    : Sequence<BRANCH_TRUE_F32, I<OPCODE_BRANCH_TRUE, VoidOp, F32Op, LabelOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.jnz(i.src2.value->name, e.T_NEAR);
  }
};
struct BRANCH_TRUE_F64
    : Sequence<BRANCH_TRUE_F64, I<OPCODE_BRANCH_TRUE, VoidOp, F64Op, LabelOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.jnz(i.src2.value->name, e.T_NEAR);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_BRANCH_TRUE, BRANCH_TRUE_I8, BRANCH_TRUE_I16,
                     BRANCH_TRUE_I32, BRANCH_TRUE_I64, BRANCH_TRUE_F32,
                     BRANCH_TRUE_F64);

// ============================================================================
// OPCODE_BRANCH_FALSE
// ============================================================================
struct BRANCH_FALSE_I8
    : Sequence<BRANCH_FALSE_I8, I<OPCODE_BRANCH_FALSE, VoidOp, I8Op, LabelOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jz(i.src2.value->name, e.T_NEAR);
  }
};
struct BRANCH_FALSE_I16
    : Sequence<BRANCH_FALSE_I16,
               I<OPCODE_BRANCH_FALSE, VoidOp, I16Op, LabelOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jz(i.src2.value->name, e.T_NEAR);
  }
};
struct BRANCH_FALSE_I32
    : Sequence<BRANCH_FALSE_I32,
               I<OPCODE_BRANCH_FALSE, VoidOp, I32Op, LabelOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jz(i.src2.value->name, e.T_NEAR);
  }
};
struct BRANCH_FALSE_I64
    : Sequence<BRANCH_FALSE_I64,
               I<OPCODE_BRANCH_FALSE, VoidOp, I64Op, LabelOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.jz(i.src2.value->name, e.T_NEAR);
  }
};
struct BRANCH_FALSE_F32
    : Sequence<BRANCH_FALSE_F32,
               I<OPCODE_BRANCH_FALSE, VoidOp, F32Op, LabelOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.jz(i.src2.value->name, e.T_NEAR);
  }
};
struct BRANCH_FALSE_F64
    : Sequence<BRANCH_FALSE_F64,
               I<OPCODE_BRANCH_FALSE, VoidOp, F64Op, LabelOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.jz(i.src2.value->name, e.T_NEAR);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_BRANCH_FALSE, BRANCH_FALSE_I8, BRANCH_FALSE_I16,
                     BRANCH_FALSE_I32, BRANCH_FALSE_I64, BRANCH_FALSE_F32,
                     BRANCH_FALSE_F64);

// ============================================================================
// OPCODE_ASSIGN
// ============================================================================
struct ASSIGN_I8 : Sequence<ASSIGN_I8, I<OPCODE_ASSIGN, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, i.src1);
  }
};
struct ASSIGN_I16 : Sequence<ASSIGN_I16, I<OPCODE_ASSIGN, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, i.src1);
  }
};
struct ASSIGN_I32 : Sequence<ASSIGN_I32, I<OPCODE_ASSIGN, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, i.src1);
  }
};
struct ASSIGN_I64 : Sequence<ASSIGN_I64, I<OPCODE_ASSIGN, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, i.src1);
  }
};
struct ASSIGN_F32 : Sequence<ASSIGN_F32, I<OPCODE_ASSIGN, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovaps(i.dest, i.src1);
  }
};
struct ASSIGN_F64 : Sequence<ASSIGN_F64, I<OPCODE_ASSIGN, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovaps(i.dest, i.src1);
  }
};
struct ASSIGN_V128 : Sequence<ASSIGN_V128, I<OPCODE_ASSIGN, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovaps(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ASSIGN, ASSIGN_I8, ASSIGN_I16, ASSIGN_I32,
                     ASSIGN_I64, ASSIGN_F32, ASSIGN_F64, ASSIGN_V128);

// ============================================================================
// OPCODE_CAST
// ============================================================================
struct CAST_I32_F32 : Sequence<CAST_I32_F32, I<OPCODE_CAST, I32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovd(i.dest, i.src1);
  }
};
struct CAST_I64_F64 : Sequence<CAST_I64_F64, I<OPCODE_CAST, I64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovq(i.dest, i.src1);
  }
};
struct CAST_F32_I32 : Sequence<CAST_F32_I32, I<OPCODE_CAST, F32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovd(i.dest, i.src1);
  }
};
struct CAST_F64_I64 : Sequence<CAST_F64_I64, I<OPCODE_CAST, F64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vmovq(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CAST, CAST_I32_F32, CAST_I64_F64, CAST_F32_I32,
                     CAST_F64_I64);

// ============================================================================
// OPCODE_ZERO_EXTEND
// ============================================================================
struct ZERO_EXTEND_I16_I8
    : Sequence<ZERO_EXTEND_I16_I8, I<OPCODE_ZERO_EXTEND, I16Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I32_I8
    : Sequence<ZERO_EXTEND_I32_I8, I<OPCODE_ZERO_EXTEND, I32Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I64_I8
    : Sequence<ZERO_EXTEND_I64_I8, I<OPCODE_ZERO_EXTEND, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I32_I16
    : Sequence<ZERO_EXTEND_I32_I16, I<OPCODE_ZERO_EXTEND, I32Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I64_I16
    : Sequence<ZERO_EXTEND_I64_I16, I<OPCODE_ZERO_EXTEND, I64Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I64_I32
    : Sequence<ZERO_EXTEND_I64_I32, I<OPCODE_ZERO_EXTEND, I64Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest.reg().cvt32(), i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ZERO_EXTEND, ZERO_EXTEND_I16_I8, ZERO_EXTEND_I32_I8,
                     ZERO_EXTEND_I64_I8, ZERO_EXTEND_I32_I16,
                     ZERO_EXTEND_I64_I16, ZERO_EXTEND_I64_I32);

// ============================================================================
// OPCODE_SIGN_EXTEND
// ============================================================================
struct SIGN_EXTEND_I16_I8
    : Sequence<SIGN_EXTEND_I16_I8, I<OPCODE_SIGN_EXTEND, I16Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsx(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I32_I8
    : Sequence<SIGN_EXTEND_I32_I8, I<OPCODE_SIGN_EXTEND, I32Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsx(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I64_I8
    : Sequence<SIGN_EXTEND_I64_I8, I<OPCODE_SIGN_EXTEND, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsx(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I32_I16
    : Sequence<SIGN_EXTEND_I32_I16, I<OPCODE_SIGN_EXTEND, I32Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsx(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I64_I16
    : Sequence<SIGN_EXTEND_I64_I16, I<OPCODE_SIGN_EXTEND, I64Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsx(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I64_I32
    : Sequence<SIGN_EXTEND_I64_I32, I<OPCODE_SIGN_EXTEND, I64Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movsxd(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SIGN_EXTEND, SIGN_EXTEND_I16_I8, SIGN_EXTEND_I32_I8,
                     SIGN_EXTEND_I64_I8, SIGN_EXTEND_I32_I16,
                     SIGN_EXTEND_I64_I16, SIGN_EXTEND_I64_I32);

// ============================================================================
// OPCODE_TRUNCATE
// ============================================================================
struct TRUNCATE_I8_I16
    : Sequence<TRUNCATE_I8_I16, I<OPCODE_TRUNCATE, I8Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest.reg().cvt32(), i.src1.reg().cvt8());
  }
};
struct TRUNCATE_I8_I32
    : Sequence<TRUNCATE_I8_I32, I<OPCODE_TRUNCATE, I8Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest.reg().cvt32(), i.src1.reg().cvt8());
  }
};
struct TRUNCATE_I8_I64
    : Sequence<TRUNCATE_I8_I64, I<OPCODE_TRUNCATE, I8Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest.reg().cvt32(), i.src1.reg().cvt8());
  }
};
struct TRUNCATE_I16_I32
    : Sequence<TRUNCATE_I16_I32, I<OPCODE_TRUNCATE, I16Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest.reg().cvt32(), i.src1.reg().cvt16());
  }
};
struct TRUNCATE_I16_I64
    : Sequence<TRUNCATE_I16_I64, I<OPCODE_TRUNCATE, I16Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.movzx(i.dest.reg().cvt32(), i.src1.reg().cvt16());
  }
};
struct TRUNCATE_I32_I64
    : Sequence<TRUNCATE_I32_I64, I<OPCODE_TRUNCATE, I32Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(i.dest, i.src1.reg().cvt32());
  }
};
EMITTER_OPCODE_TABLE(OPCODE_TRUNCATE, TRUNCATE_I8_I16, TRUNCATE_I8_I32,
                     TRUNCATE_I8_I64, TRUNCATE_I16_I32, TRUNCATE_I16_I64,
                     TRUNCATE_I32_I64);

// ============================================================================
// OPCODE_CONVERT
// ============================================================================
struct CONVERT_I32_F32
    : Sequence<CONVERT_I32_F32, I<OPCODE_CONVERT, I32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): saturation check? cvtt* (trunc?)
    if (i.instr->flags == ROUND_TO_ZERO) {
      e.vcvttss2si(i.dest, i.src1);
    } else {
      e.vcvtss2si(i.dest, i.src1);
    }
  }
};
struct CONVERT_I32_F64
    : Sequence<CONVERT_I32_F64, I<OPCODE_CONVERT, I32Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // Intel returns 0x80000000 if the double value does not fit within an int32
    // PPC saturates the value instead.
    // So, we can clamp the double value to (double)0x7FFFFFFF.
    e.vminsd(e.xmm0, i.src1, e.GetXmmConstPtr(XMMIntMaxPD));
    if (i.instr->flags == ROUND_TO_ZERO) {
      e.vcvttsd2si(i.dest, e.xmm0);
    } else {
      e.vcvtsd2si(i.dest, e.xmm0);
    }
  }
};
struct CONVERT_I64_F64
    : Sequence<CONVERT_I64_F64, I<OPCODE_CONVERT, I64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // Copy src1.
    e.movq(e.rcx, i.src1);

    // TODO(benvanik): saturation check? cvtt* (trunc?)
    if (i.instr->flags == ROUND_TO_ZERO) {
      e.vcvttsd2si(i.dest, i.src1);
    } else {
      e.vcvtsd2si(i.dest, i.src1);
    }

    // 0x8000000000000000
    e.mov(e.rax, 0x1);
    e.shl(e.rax, 63);

    // Saturate positive overflow
    // TODO(DrChat): Find a shorter equivalent sequence.
    // if (result ind. && src1 >= 0)
    //   result = 0x7FFFFFFFFFFFFFFF;
    e.cmp(e.rax, i.dest);
    e.sete(e.al);
    e.movzx(e.rax, e.al);
    e.shr(e.rcx, 63);
    e.xor_(e.rcx, 0x01);
    e.and_(e.rax, e.rcx);

    e.sub(i.dest, e.rax);
  }
};
struct CONVERT_F32_I32
    : Sequence<CONVERT_F32_I32, I<OPCODE_CONVERT, F32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): saturation check? cvtt* (trunc?)
    e.vcvtsi2ss(i.dest, i.src1);
  }
};
struct CONVERT_F32_F64
    : Sequence<CONVERT_F32_F64, I<OPCODE_CONVERT, F32Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): saturation check? cvtt* (trunc?)
    e.vcvtsd2ss(i.dest, i.src1);
  }
};
struct CONVERT_F64_I64
    : Sequence<CONVERT_F64_I64, I<OPCODE_CONVERT, F64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): saturation check? cvtt* (trunc?)
    e.vcvtsi2sd(i.dest, i.src1);
  }
};
struct CONVERT_F64_F32
    : Sequence<CONVERT_F64_F32, I<OPCODE_CONVERT, F64Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vcvtss2sd(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CONVERT, CONVERT_I32_F32, CONVERT_I32_F64,
                     CONVERT_I64_F64, CONVERT_F32_I32, CONVERT_F32_F64,
                     CONVERT_F64_I64, CONVERT_F64_F32);

// ============================================================================
// OPCODE_ROUND
// ============================================================================
struct ROUND_F32 : Sequence<ROUND_F32, I<OPCODE_ROUND, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case ROUND_TO_ZERO:
        e.vroundss(i.dest, i.src1, 0b00000011);
        break;
      case ROUND_TO_NEAREST:
        e.vroundss(i.dest, i.src1, 0b00000000);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.vroundss(i.dest, i.src1, 0b00000001);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.vroundss(i.dest, i.src1, 0b00000010);
        break;
    }
  }
};
struct ROUND_F64 : Sequence<ROUND_F64, I<OPCODE_ROUND, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case ROUND_TO_ZERO:
        e.vroundsd(i.dest, i.src1, 0b00000011);
        break;
      case ROUND_TO_NEAREST:
        e.vroundsd(i.dest, i.src1, 0b00000000);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.vroundsd(i.dest, i.src1, 0b00000001);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.vroundsd(i.dest, i.src1, 0b00000010);
        break;
    }
  }
};
struct ROUND_V128 : Sequence<ROUND_V128, I<OPCODE_ROUND, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case ROUND_TO_ZERO:
        e.vroundps(i.dest, i.src1, 0b00000011);
        break;
      case ROUND_TO_NEAREST:
        e.vroundps(i.dest, i.src1, 0b00000000);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.vroundps(i.dest, i.src1, 0b00000001);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.vroundps(i.dest, i.src1, 0b00000010);
        break;
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ROUND, ROUND_F32, ROUND_F64, ROUND_V128);

// ============================================================================
// OPCODE_VECTOR_CONVERT_I2F
// ============================================================================
struct VECTOR_CONVERT_I2F
    : Sequence<VECTOR_CONVERT_I2F,
               I<OPCODE_VECTOR_CONVERT_I2F, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // flags = ARITHMETIC_UNSIGNED
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      // xmm0 = mask of positive values
      e.vpcmpgtd(e.xmm0, i.src1, e.GetXmmConstPtr(XMMFFFF));

      // scale any values >= (unsigned)INT_MIN back to [0, INT_MAX]
      e.vpsubd(e.xmm1, i.src1, e.GetXmmConstPtr(XMMSignMaskI32));
      e.vblendvps(e.xmm1, e.xmm1, i.src1, e.xmm0);

      // xmm1 = [0, INT_MAX]
      e.vcvtdq2ps(i.dest, e.xmm1);

      // scale values back above [INT_MIN, UINT_MAX]
      e.vpandn(e.xmm0, e.xmm0, e.GetXmmConstPtr(XMMPosIntMinPS));
      e.vaddps(i.dest, i.dest, e.xmm0);
    } else {
      e.vcvtdq2ps(i.dest, i.src1);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_CONVERT_I2F, VECTOR_CONVERT_I2F);

// ============================================================================
// OPCODE_VECTOR_CONVERT_F2I
// ============================================================================
struct VECTOR_CONVERT_F2I
    : Sequence<VECTOR_CONVERT_F2I,
               I<OPCODE_VECTOR_CONVERT_F2I, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      // clamp to min 0
      e.vmaxps(e.xmm0, i.src1, e.GetXmmConstPtr(XMMZero));

      // xmm1 = mask of values >= (unsigned)INT_MIN
      e.vcmpgeps(e.xmm1, e.xmm0, e.GetXmmConstPtr(XMMPosIntMinPS));

      // scale any values >= (unsigned)INT_MIN back to [0, ...]
      e.vsubps(e.xmm2, e.xmm0, e.GetXmmConstPtr(XMMPosIntMinPS));
      e.vblendvps(e.xmm0, e.xmm0, e.xmm2, e.xmm1);

      // xmm0 = [0, INT_MAX]
      // this may still contain values > INT_MAX (if src has vals > UINT_MAX)
      e.vcvttps2dq(i.dest, e.xmm0);

      // xmm0 = mask of values that need saturation
      e.vpcmpeqd(e.xmm0, i.dest, e.GetXmmConstPtr(XMMIntMin));

      // scale values back above [INT_MIN, UINT_MAX]
      e.vpand(e.xmm1, e.xmm1, e.GetXmmConstPtr(XMMIntMin));
      e.vpaddd(i.dest, i.dest, e.xmm1);

      // saturate values > UINT_MAX
      e.vpor(i.dest, i.dest, e.xmm0);
    } else {
      // xmm2 = NaN mask
      e.vcmpunordps(e.xmm2, i.src1, i.src1);

      // convert packed floats to packed dwords
      e.vcvttps2dq(e.xmm0, i.src1);

      // (high bit) xmm1 = dest is indeterminate and i.src1 >= 0
      e.vpcmpeqd(e.xmm1, e.xmm0, e.GetXmmConstPtr(XMMIntMin));
      e.vpandn(e.xmm1, i.src1, e.xmm1);

      // saturate positive values
      e.vblendvps(i.dest, e.xmm0, e.GetXmmConstPtr(XMMIntMax), e.xmm1);

      // mask NaNs
      e.vpandn(i.dest, e.xmm2, i.dest);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_CONVERT_F2I, VECTOR_CONVERT_F2I);

// ============================================================================
// OPCODE_LOAD_VECTOR_SHL
// ============================================================================
static const vec128_t lvsl_table[16] = {
    vec128b(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
    vec128b(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16),
    vec128b(2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17),
    vec128b(3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18),
    vec128b(4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19),
    vec128b(5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20),
    vec128b(6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21),
    vec128b(7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22),
    vec128b(8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23),
    vec128b(9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24),
    vec128b(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25),
    vec128b(11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26),
    vec128b(12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
    vec128b(13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28),
    vec128b(14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29),
    vec128b(15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30),
};
struct LOAD_VECTOR_SHL_I8
    : Sequence<LOAD_VECTOR_SHL_I8, I<OPCODE_LOAD_VECTOR_SHL, V128Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      auto sh = i.src1.constant();
      assert_true(sh < xe::countof(lvsl_table));
      e.mov(e.rax, (uintptr_t)&lvsl_table[sh]);
      e.vmovaps(i.dest, e.ptr[e.rax]);
    } else {
      // TODO(benvanik): find a cheaper way of doing this.
      e.movzx(e.rdx, i.src1);
      e.and_(e.dx, 0xF);
      e.shl(e.dx, 4);
      e.mov(e.rax, (uintptr_t)lvsl_table);
      e.vmovaps(i.dest, e.ptr[e.rax + e.rdx]);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_VECTOR_SHL, LOAD_VECTOR_SHL_I8);

// ============================================================================
// OPCODE_LOAD_VECTOR_SHR
// ============================================================================
static const vec128_t lvsr_table[16] = {
    vec128b(16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31),
    vec128b(15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30),
    vec128b(14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29),
    vec128b(13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28),
    vec128b(12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
    vec128b(11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26),
    vec128b(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25),
    vec128b(9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24),
    vec128b(8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23),
    vec128b(7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22),
    vec128b(6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21),
    vec128b(5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20),
    vec128b(4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19),
    vec128b(3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18),
    vec128b(2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17),
    vec128b(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16),
};
struct LOAD_VECTOR_SHR_I8
    : Sequence<LOAD_VECTOR_SHR_I8, I<OPCODE_LOAD_VECTOR_SHR, V128Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      auto sh = i.src1.constant();
      assert_true(sh < xe::countof(lvsr_table));
      e.mov(e.rax, (uintptr_t)&lvsr_table[sh]);
      e.vmovaps(i.dest, e.ptr[e.rax]);
    } else {
      // TODO(benvanik): find a cheaper way of doing this.
      e.movzx(e.rdx, i.src1);
      e.and_(e.dx, 0xF);
      e.shl(e.dx, 4);
      e.mov(e.rax, (uintptr_t)lvsr_table);
      e.vmovaps(i.dest, e.ptr[e.rax + e.rdx]);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_VECTOR_SHR, LOAD_VECTOR_SHR_I8);

// ============================================================================
// OPCODE_LOAD_CLOCK
// ============================================================================
struct LOAD_CLOCK : Sequence<LOAD_CLOCK, I<OPCODE_LOAD_CLOCK, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // It'd be cool to call QueryPerformanceCounter directly, but w/e.
    e.CallNative(LoadClock);
    e.mov(i.dest, e.rax);
  }
  static uint64_t LoadClock(void* raw_context) {
    return Clock::QueryGuestTickCount();
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_CLOCK, LOAD_CLOCK);

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
// Note: all types are always aligned in the context.
RegExp ComputeContextAddress(X64Emitter& e, const OffsetOp& offset) {
  return e.GetContextReg() + offset.value;
}
struct LOAD_CONTEXT_I8
    : Sequence<LOAD_CONTEXT_I8, I<OPCODE_LOAD_CONTEXT, I8Op, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeContextAddress(e, i.src1);
    e.mov(i.dest, e.byte[addr]);
    if (IsTracingData()) {
      e.mov(e.r8, e.byte[addr]);
      e.mov(e.rdx, i.src1.value);
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
      e.mov(e.r8, e.word[addr]);
      e.mov(e.rdx, i.src1.value);
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
      e.mov(e.r8, e.dword[addr]);
      e.mov(e.rdx, i.src1.value);
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
      e.mov(e.r8, e.qword[addr]);
      e.mov(e.rdx, i.src1.value);
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
      e.lea(e.r8, e.dword[addr]);
      e.mov(e.rdx, i.src1.value);
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
      e.lea(e.r8, e.qword[addr]);
      e.mov(e.rdx, i.src1.value);
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
      e.lea(e.r8, e.ptr[addr]);
      e.mov(e.rdx, i.src1.value);
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
      e.mov(e.r8, e.byte[addr]);
      e.mov(e.rdx, i.src1.value);
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
      e.mov(e.r8, e.word[addr]);
      e.mov(e.rdx, i.src1.value);
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
      e.mov(e.r8, e.dword[addr]);
      e.mov(e.rdx, i.src1.value);
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
      e.mov(e.r8, e.qword[addr]);
      e.mov(e.rdx, i.src1.value);
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
      e.lea(e.r8, e.dword[addr]);
      e.mov(e.rdx, i.src1.value);
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
      e.lea(e.r8, e.qword[addr]);
      e.mov(e.rdx, i.src1.value);
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
      e.lea(e.r8, e.ptr[addr]);
      e.mov(e.rdx, i.src1.value);
      e.CallNative(reinterpret_cast<void*>(TraceContextStoreV128));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE_CONTEXT, STORE_CONTEXT_I8, STORE_CONTEXT_I16,
                     STORE_CONTEXT_I32, STORE_CONTEXT_I64, STORE_CONTEXT_F32,
                     STORE_CONTEXT_F64, STORE_CONTEXT_V128);

// ============================================================================
// OPCODE_CONTEXT_BARRIER
// ============================================================================
struct CONTEXT_BARRIER
    : Sequence<CONTEXT_BARRIER, I<OPCODE_CONTEXT_BARRIER, VoidOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {}
};
EMITTER_OPCODE_TABLE(OPCODE_CONTEXT_BARRIER, CONTEXT_BARRIER);

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
    e.mov(e.r8, uint64_t(mmio_range->callback_context));
    e.mov(e.r9d, read_address);
    e.CallNativeSafe(reinterpret_cast<void*>(mmio_range->read));
    e.bswap(e.eax);
    e.mov(i.dest, e.eax);
    if (IsTracingData()) {
      e.mov(e.r8, i.dest);
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
    e.mov(e.r8, uint64_t(mmio_range->callback_context));
    e.mov(e.r9d, write_address);
    if (i.src3.is_constant) {
      e.mov(e.r10d, xe::byte_swap(i.src3.constant()));
    } else {
      e.mov(e.r10d, i.src3);
      e.bswap(e.r10d);
    }
    e.CallNativeSafe(reinterpret_cast<void*>(mmio_range->write));
    if (IsTracingData()) {
      if (i.src3.is_constant) {
        e.mov(e.r8d, i.src3.constant());
      } else {
        e.mov(e.r8d, i.src3);
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
template <typename T>
RegExp ComputeMemoryAddressOffset(X64Emitter& e, const T& guest,
                                  const T& offset) {
  int32_t offset_const = static_cast<int32_t>(offset.constant());

  if (guest.is_constant) {
    uint32_t address = static_cast<uint32_t>(guest.constant());
    address += static_cast<int32_t>(offset.constant());
    if (address < 0x80000000) {
      return e.GetMembaseReg() + address;
    } else {
      e.mov(e.eax, address);
      return e.GetMembaseReg() + e.rax;
    }
  } else {
    // Clear the top 32 bits, as they are likely garbage.
    // TODO(benvanik): find a way to avoid doing this.
    e.mov(e.eax, guest.reg().cvt32());
    return e.GetMembaseReg() + e.rax + offset_const;
  }
}

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
      e.mov(e.eax, address);
      return e.GetMembaseReg() + e.rax;
    }
  } else {
    // Clear the top 32 bits, as they are likely garbage.
    // TODO(benvanik): find a way to avoid doing this.
    e.mov(e.eax, guest.reg().cvt32());
    return e.GetMembaseReg() + e.rax;
  }
}
struct LOAD_I8 : Sequence<LOAD_I8, I<OPCODE_LOAD, I8Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto addr = ComputeMemoryAddress(e, i.src1);
    e.mov(i.dest, e.byte[addr]);
    if (IsTracingData()) {
      e.mov(e.r8b, i.dest);
      e.lea(e.rdx, e.ptr[addr]);
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
      e.mov(e.r8w, i.dest);
      e.lea(e.rdx, e.ptr[addr]);
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
      e.mov(e.r8d, i.dest);
      e.lea(e.rdx, e.ptr[addr]);
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
      e.mov(e.r8, i.dest);
      e.lea(e.rdx, e.ptr[addr]);
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
      e.lea(e.r8, e.dword[addr]);
      e.lea(e.rdx, e.ptr[addr]);
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
      e.lea(e.r8, e.qword[addr]);
      e.lea(e.rdx, e.ptr[addr]);
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
      e.lea(e.r8, e.ptr[addr]);
      e.lea(e.rdx, e.ptr[addr]);
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
      e.mov(e.r8b, e.byte[addr]);
      e.lea(e.rdx, e.ptr[addr]);
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
      e.mov(e.r8w, e.word[addr]);
      e.lea(e.rdx, e.ptr[addr]);
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
      e.mov(e.r8d, e.dword[addr]);
      e.lea(e.rdx, e.ptr[addr]);
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
      e.mov(e.r8, e.qword[addr]);
      e.lea(e.rdx, e.ptr[addr]);
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
      e.lea(e.r8, e.ptr[addr]);
      e.lea(e.rdx, e.ptr[addr]);
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
      e.lea(e.r8, e.ptr[addr]);
      e.lea(e.rdx, e.ptr[addr]);
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
      e.lea(e.r8, e.ptr[addr]);
      e.lea(e.rdx, e.ptr[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceMemoryStoreV128));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE, STORE_I8, STORE_I16, STORE_I32, STORE_I64,
                     STORE_F32, STORE_F64, STORE_V128);

// ============================================================================
// OPCODE_PREFETCH
// ============================================================================
struct PREFETCH
    : Sequence<PREFETCH, I<OPCODE_PREFETCH, VoidOp, I64Op, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): prefetch addr -> length.
  }
};
EMITTER_OPCODE_TABLE(OPCODE_PREFETCH, PREFETCH);

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
      e.mov(e.r9, i.src3.constant());
      e.mov(e.r8, i.src2.constant());
      e.lea(e.rdx, e.ptr[addr]);
      e.CallNative(reinterpret_cast<void*>(TraceMemset));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MEMSET, MEMSET_I64_I8_I64);

// ============================================================================
// OPCODE_MAX
// ============================================================================
struct MAX_F32 : Sequence<MAX_F32, I<OPCODE_MAX, F32Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vmaxss(dest, src1, src2);
                               });
  }
};
struct MAX_F64 : Sequence<MAX_F64, I<OPCODE_MAX, F64Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vmaxsd(dest, src1, src2);
                               });
  }
};
struct MAX_V128 : Sequence<MAX_V128, I<OPCODE_MAX, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vmaxps(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MAX, MAX_F32, MAX_F64, MAX_V128);

// ============================================================================
// OPCODE_VECTOR_MAX
// ============================================================================
struct VECTOR_MAX
    : Sequence<VECTOR_MAX, I<OPCODE_VECTOR_MAX, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          uint32_t part_type = i.instr->flags >> 8;
          if (i.instr->flags & ARITHMETIC_UNSIGNED) {
            switch (part_type) {
              case INT8_TYPE:
                e.vpmaxub(dest, src1, src2);
                break;
              case INT16_TYPE:
                e.vpmaxuw(dest, src1, src2);
                break;
              case INT32_TYPE:
                e.vpmaxud(dest, src1, src2);
                break;
              default:
                assert_unhandled_case(part_type);
                break;
            }
          } else {
            switch (part_type) {
              case INT8_TYPE:
                e.vpmaxsb(dest, src1, src2);
                break;
              case INT16_TYPE:
                e.vpmaxsw(dest, src1, src2);
                break;
              case INT32_TYPE:
                e.vpmaxsd(dest, src1, src2);
                break;
              default:
                assert_unhandled_case(part_type);
                break;
            }
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_MAX, VECTOR_MAX);

// ============================================================================
// OPCODE_MIN
// ============================================================================
struct MIN_I8 : Sequence<MIN_I8, I<OPCODE_MIN, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryOp(
        e, i,
        [](X64Emitter& e, const Reg8& dest_src, const Reg8& src) {
          e.cmp(dest_src, src);
          e.cmovg(dest_src.cvt32(), src.cvt32());
        },
        [](X64Emitter& e, const Reg8& dest_src, int32_t constant) {
          e.mov(e.al, constant);
          e.cmp(dest_src, e.al);
          e.cmovg(dest_src.cvt32(), e.eax);
        });
  }
};
struct MIN_I16 : Sequence<MIN_I16, I<OPCODE_MIN, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryOp(
        e, i,
        [](X64Emitter& e, const Reg16& dest_src, const Reg16& src) {
          e.cmp(dest_src, src);
          e.cmovg(dest_src.cvt32(), src.cvt32());
        },
        [](X64Emitter& e, const Reg16& dest_src, int32_t constant) {
          e.mov(e.ax, constant);
          e.cmp(dest_src, e.ax);
          e.cmovg(dest_src.cvt32(), e.eax);
        });
  }
};
struct MIN_I32 : Sequence<MIN_I32, I<OPCODE_MIN, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryOp(
        e, i,
        [](X64Emitter& e, const Reg32& dest_src, const Reg32& src) {
          e.cmp(dest_src, src);
          e.cmovg(dest_src, src);
        },
        [](X64Emitter& e, const Reg32& dest_src, int32_t constant) {
          e.mov(e.eax, constant);
          e.cmp(dest_src, e.eax);
          e.cmovg(dest_src, e.eax);
        });
  }
};
struct MIN_I64 : Sequence<MIN_I64, I<OPCODE_MIN, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryOp(
        e, i,
        [](X64Emitter& e, const Reg64& dest_src, const Reg64& src) {
          e.cmp(dest_src, src);
          e.cmovg(dest_src, src);
        },
        [](X64Emitter& e, const Reg64& dest_src, int64_t constant) {
          e.mov(e.rax, constant);
          e.cmp(dest_src, e.rax);
          e.cmovg(dest_src, e.rax);
        });
  }
};
struct MIN_F32 : Sequence<MIN_F32, I<OPCODE_MIN, F32Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vminss(dest, src1, src2);
                               });
  }
};
struct MIN_F64 : Sequence<MIN_F64, I<OPCODE_MIN, F64Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vminsd(dest, src1, src2);
                               });
  }
};
struct MIN_V128 : Sequence<MIN_V128, I<OPCODE_MIN, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vminps(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MIN, MIN_I8, MIN_I16, MIN_I32, MIN_I64, MIN_F32,
                     MIN_F64, MIN_V128);

// ============================================================================
// OPCODE_VECTOR_MIN
// ============================================================================
struct VECTOR_MIN
    : Sequence<VECTOR_MIN, I<OPCODE_VECTOR_MIN, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
          uint32_t part_type = i.instr->flags >> 8;
          if (i.instr->flags & ARITHMETIC_UNSIGNED) {
            switch (part_type) {
              case INT8_TYPE:
                e.vpminub(dest, src1, src2);
                break;
              case INT16_TYPE:
                e.vpminuw(dest, src1, src2);
                break;
              case INT32_TYPE:
                e.vpminud(dest, src1, src2);
                break;
              default:
                assert_unhandled_case(part_type);
                break;
            }
          } else {
            switch (part_type) {
              case INT8_TYPE:
                e.vpminsb(dest, src1, src2);
                break;
              case INT16_TYPE:
                e.vpminsw(dest, src1, src2);
                break;
              case INT32_TYPE:
                e.vpminsd(dest, src1, src2);
                break;
              default:
                assert_unhandled_case(part_type);
                break;
            }
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_MIN, VECTOR_MIN);

// ============================================================================
// OPCODE_SELECT
// ============================================================================
// dest = src1 ? src2 : src3
// TODO(benvanik): match compare + select sequences, as often it's something
//     like SELECT(VECTOR_COMPARE_SGE(a, b), a, b)
struct SELECT_I8
    : Sequence<SELECT_I8, I<OPCODE_SELECT, I8Op, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Reg8 src2;
    if (i.src2.is_constant) {
      src2 = e.al;
      e.mov(src2, i.src2.constant());
    } else {
      src2 = i.src2;
    }
    e.test(i.src1, i.src1);
    e.cmovnz(i.dest.reg().cvt32(), src2.cvt32());
    e.cmovz(i.dest.reg().cvt32(), i.src3.reg().cvt32());
  }
};
struct SELECT_I16
    : Sequence<SELECT_I16, I<OPCODE_SELECT, I16Op, I8Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Reg16 src2;
    if (i.src2.is_constant) {
      src2 = e.ax;
      e.mov(src2, i.src2.constant());
    } else {
      src2 = i.src2;
    }
    e.test(i.src1, i.src1);
    e.cmovnz(i.dest.reg().cvt32(), src2.cvt32());
    e.cmovz(i.dest.reg().cvt32(), i.src3.reg().cvt32());
  }
};
struct SELECT_I32
    : Sequence<SELECT_I32, I<OPCODE_SELECT, I32Op, I8Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Reg32 src2;
    if (i.src2.is_constant) {
      src2 = e.eax;
      e.mov(src2, i.src2.constant());
    } else {
      src2 = i.src2;
    }
    e.test(i.src1, i.src1);
    e.cmovnz(i.dest, src2);
    e.cmovz(i.dest, i.src3);
  }
};
struct SELECT_I64
    : Sequence<SELECT_I64, I<OPCODE_SELECT, I64Op, I8Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Reg64 src2;
    if (i.src2.is_constant) {
      src2 = e.rax;
      e.mov(src2, i.src2.constant());
    } else {
      src2 = i.src2;
    }
    e.test(i.src1, i.src1);
    e.cmovnz(i.dest, src2);
    e.cmovz(i.dest, i.src3);
  }
};
struct SELECT_F32
    : Sequence<SELECT_F32, I<OPCODE_SELECT, F32Op, I8Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): find a shorter sequence.
    // dest = src1 != 0 ? src2 : src3
    e.movzx(e.eax, i.src1);
    e.vmovd(e.xmm1, e.eax);
    e.vxorps(e.xmm0, e.xmm0);
    e.vpcmpeqd(e.xmm0, e.xmm1);

    Xmm src2 = i.src2.is_constant ? e.xmm2 : i.src2;
    if (i.src2.is_constant) {
      e.LoadConstantXmm(src2, i.src2.constant());
    }
    e.vpandn(e.xmm1, e.xmm0, src2);

    Xmm src3 = i.src3.is_constant ? e.xmm2 : i.src3;
    if (i.src3.is_constant) {
      e.LoadConstantXmm(src3, i.src3.constant());
    }
    e.vpand(i.dest, e.xmm0, src3);
    e.vpor(i.dest, e.xmm1);
  }
};
struct SELECT_F64
    : Sequence<SELECT_F64, I<OPCODE_SELECT, F64Op, I8Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // dest = src1 != 0 ? src2 : src3
    e.movzx(e.eax, i.src1);
    e.vmovd(e.xmm1, e.eax);
    e.vpxor(e.xmm0, e.xmm0);
    e.vpcmpeqq(e.xmm0, e.xmm1);

    Xmm src2 = i.src2.is_constant ? e.xmm2 : i.src2;
    if (i.src2.is_constant) {
      e.LoadConstantXmm(src2, i.src2.constant());
    }
    e.vpandn(e.xmm1, e.xmm0, src2);

    Xmm src3 = i.src3.is_constant ? e.xmm2 : i.src3;
    if (i.src3.is_constant) {
      e.LoadConstantXmm(src3, i.src3.constant());
    }
    e.vpand(i.dest, e.xmm0, src3);
    e.vpor(i.dest, e.xmm1);
  }
};
struct SELECT_V128_I8
    : Sequence<SELECT_V128_I8, I<OPCODE_SELECT, V128Op, I8Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): find a shorter sequence.
    // dest = src1 != 0 ? src2 : src3
    e.movzx(e.eax, i.src1);
    e.vmovd(e.xmm1, e.eax);
    e.vpbroadcastd(e.xmm1, e.xmm1);
    e.vxorps(e.xmm0, e.xmm0);
    e.vpcmpeqd(e.xmm0, e.xmm1);

    Xmm src2 = i.src2.is_constant ? e.xmm2 : i.src2;
    if (i.src2.is_constant) {
      e.LoadConstantXmm(src2, i.src2.constant());
    }
    e.vpandn(e.xmm1, e.xmm0, src2);

    Xmm src3 = i.src3.is_constant ? e.xmm2 : i.src3;
    if (i.src3.is_constant) {
      e.LoadConstantXmm(src3, i.src3.constant());
    }
    e.vpand(i.dest, e.xmm0, src3);
    e.vpor(i.dest, e.xmm1);
  }
};
struct SELECT_V128_V128
    : Sequence<SELECT_V128_V128,
               I<OPCODE_SELECT, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Xmm src1 = i.src1.is_constant ? e.xmm0 : i.src1;
    if (i.src1.is_constant) {
      e.LoadConstantXmm(src1, i.src1.constant());
    }

    Xmm src2 = i.src2.is_constant ? e.xmm1 : i.src2;
    if (i.src2.is_constant) {
      e.LoadConstantXmm(src2, i.src2.constant());
    }

    Xmm src3 = i.src3.is_constant ? e.xmm2 : i.src3;
    if (i.src3.is_constant) {
      e.LoadConstantXmm(src3, i.src3.constant());
    }

    // src1 ? src2 : src3;
    e.vpandn(e.xmm3, src1, src2);
    e.vpand(i.dest, src1, src3);
    e.vpor(i.dest, i.dest, e.xmm3);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SELECT, SELECT_I8, SELECT_I16, SELECT_I32,
                     SELECT_I64, SELECT_F32, SELECT_F64, SELECT_V128_I8,
                     SELECT_V128_V128);

// ============================================================================
// OPCODE_IS_TRUE
// ============================================================================
struct IS_TRUE_I8 : Sequence<IS_TRUE_I8, I<OPCODE_IS_TRUE, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
struct IS_TRUE_I16 : Sequence<IS_TRUE_I16, I<OPCODE_IS_TRUE, I8Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
struct IS_TRUE_I32 : Sequence<IS_TRUE_I32, I<OPCODE_IS_TRUE, I8Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
struct IS_TRUE_I64 : Sequence<IS_TRUE_I64, I<OPCODE_IS_TRUE, I8Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
struct IS_TRUE_F32 : Sequence<IS_TRUE_F32, I<OPCODE_IS_TRUE, I8Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
struct IS_TRUE_F64 : Sequence<IS_TRUE_F64, I<OPCODE_IS_TRUE, I8Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
struct IS_TRUE_V128 : Sequence<IS_TRUE_V128, I<OPCODE_IS_TRUE, I8Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setnz(i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_IS_TRUE, IS_TRUE_I8, IS_TRUE_I16, IS_TRUE_I32,
                     IS_TRUE_I64, IS_TRUE_F32, IS_TRUE_F64, IS_TRUE_V128);

// ============================================================================
// OPCODE_IS_FALSE
// ============================================================================
struct IS_FALSE_I8 : Sequence<IS_FALSE_I8, I<OPCODE_IS_FALSE, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setz(i.dest);
  }
};
struct IS_FALSE_I16 : Sequence<IS_FALSE_I16, I<OPCODE_IS_FALSE, I8Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setz(i.dest);
  }
};
struct IS_FALSE_I32 : Sequence<IS_FALSE_I32, I<OPCODE_IS_FALSE, I8Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setz(i.dest);
  }
};
struct IS_FALSE_I64 : Sequence<IS_FALSE_I64, I<OPCODE_IS_FALSE, I8Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.test(i.src1, i.src1);
    e.setz(i.dest);
  }
};
struct IS_FALSE_F32 : Sequence<IS_FALSE_F32, I<OPCODE_IS_FALSE, I8Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setz(i.dest);
  }
};
struct IS_FALSE_F64 : Sequence<IS_FALSE_F64, I<OPCODE_IS_FALSE, I8Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setz(i.dest);
  }
};
struct IS_FALSE_V128
    : Sequence<IS_FALSE_V128, I<OPCODE_IS_FALSE, I8Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vptest(i.src1, i.src1);
    e.setz(i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_IS_FALSE, IS_FALSE_I8, IS_FALSE_I16, IS_FALSE_I32,
                     IS_FALSE_I64, IS_FALSE_F32, IS_FALSE_F64, IS_FALSE_V128);

// ============================================================================
// OPCODE_IS_NAN
// ============================================================================
struct IS_NAN_F32 : Sequence<IS_NAN_F32, I<OPCODE_IS_NAN, I8Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vucomiss(i.src1, i.src1);
    e.setp(i.dest);
  }
};

struct IS_NAN_F64 : Sequence<IS_NAN_F64, I<OPCODE_IS_NAN, I8Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vucomisd(i.src1, i.src1);
    e.setp(i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_IS_NAN, IS_NAN_F32, IS_NAN_F64);

// ============================================================================
// OPCODE_COMPARE_EQ
// ============================================================================
struct COMPARE_EQ_I8
    : Sequence<COMPARE_EQ_I8, I<OPCODE_COMPARE_EQ, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(e, i,
                             [](X64Emitter& e, const Reg8& src1,
                                const Reg8& src2) { e.cmp(src1, src2); },
                             [](X64Emitter& e, const Reg8& src1,
                                int32_t constant) { e.cmp(src1, constant); });
    e.sete(i.dest);
  }
};
struct COMPARE_EQ_I16
    : Sequence<COMPARE_EQ_I16, I<OPCODE_COMPARE_EQ, I8Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(e, i,
                             [](X64Emitter& e, const Reg16& src1,
                                const Reg16& src2) { e.cmp(src1, src2); },
                             [](X64Emitter& e, const Reg16& src1,
                                int32_t constant) { e.cmp(src1, constant); });
    e.sete(i.dest);
  }
};
struct COMPARE_EQ_I32
    : Sequence<COMPARE_EQ_I32, I<OPCODE_COMPARE_EQ, I8Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(e, i,
                             [](X64Emitter& e, const Reg32& src1,
                                const Reg32& src2) { e.cmp(src1, src2); },
                             [](X64Emitter& e, const Reg32& src1,
                                int32_t constant) { e.cmp(src1, constant); });
    e.sete(i.dest);
  }
};
struct COMPARE_EQ_I64
    : Sequence<COMPARE_EQ_I64, I<OPCODE_COMPARE_EQ, I8Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(e, i,
                             [](X64Emitter& e, const Reg64& src1,
                                const Reg64& src2) { e.cmp(src1, src2); },
                             [](X64Emitter& e, const Reg64& src1,
                                int32_t constant) { e.cmp(src1, constant); });
    e.sete(i.dest);
  }
};
struct COMPARE_EQ_F32
    : Sequence<COMPARE_EQ_F32, I<OPCODE_COMPARE_EQ, I8Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, I8Op dest, const Xmm& src1, const Xmm& src2) {
          e.vcomiss(src1, src2);
        });
    e.sete(i.dest);
  }
};
struct COMPARE_EQ_F64
    : Sequence<COMPARE_EQ_F64, I<OPCODE_COMPARE_EQ, I8Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, I8Op dest, const Xmm& src1, const Xmm& src2) {
          e.vcomisd(src1, src2);
        });
    e.sete(i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_EQ, COMPARE_EQ_I8, COMPARE_EQ_I16,
                     COMPARE_EQ_I32, COMPARE_EQ_I64, COMPARE_EQ_F32,
                     COMPARE_EQ_F64);

// ============================================================================
// OPCODE_COMPARE_NE
// ============================================================================
struct COMPARE_NE_I8
    : Sequence<COMPARE_NE_I8, I<OPCODE_COMPARE_NE, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(e, i,
                             [](X64Emitter& e, const Reg8& src1,
                                const Reg8& src2) { e.cmp(src1, src2); },
                             [](X64Emitter& e, const Reg8& src1,
                                int32_t constant) { e.cmp(src1, constant); });
    e.setne(i.dest);
  }
};
struct COMPARE_NE_I16
    : Sequence<COMPARE_NE_I16, I<OPCODE_COMPARE_NE, I8Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(e, i,
                             [](X64Emitter& e, const Reg16& src1,
                                const Reg16& src2) { e.cmp(src1, src2); },
                             [](X64Emitter& e, const Reg16& src1,
                                int32_t constant) { e.cmp(src1, constant); });
    e.setne(i.dest);
  }
};
struct COMPARE_NE_I32
    : Sequence<COMPARE_NE_I32, I<OPCODE_COMPARE_NE, I8Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(e, i,
                             [](X64Emitter& e, const Reg32& src1,
                                const Reg32& src2) { e.cmp(src1, src2); },
                             [](X64Emitter& e, const Reg32& src1,
                                int32_t constant) { e.cmp(src1, constant); });
    e.setne(i.dest);
  }
};
struct COMPARE_NE_I64
    : Sequence<COMPARE_NE_I64, I<OPCODE_COMPARE_NE, I8Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeCompareOp(e, i,
                             [](X64Emitter& e, const Reg64& src1,
                                const Reg64& src2) { e.cmp(src1, src2); },
                             [](X64Emitter& e, const Reg64& src1,
                                int32_t constant) { e.cmp(src1, constant); });
    e.setne(i.dest);
  }
};
struct COMPARE_NE_F32
    : Sequence<COMPARE_NE_F32, I<OPCODE_COMPARE_NE, I8Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vcomiss(i.src1, i.src2);
    e.setne(i.dest);
  }
};
struct COMPARE_NE_F64
    : Sequence<COMPARE_NE_F64, I<OPCODE_COMPARE_NE, I8Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vcomisd(i.src1, i.src2);
    e.setne(i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_NE, COMPARE_NE_I8, COMPARE_NE_I16,
                     COMPARE_NE_I32, COMPARE_NE_I64, COMPARE_NE_F32,
                     COMPARE_NE_F64);

// ============================================================================
// OPCODE_COMPARE_*
// ============================================================================
#define EMITTER_ASSOCIATIVE_COMPARE_INT(op, instr, inverse_instr, type, \
                                        reg_type)                       \
  struct COMPARE_##op##_##type                                          \
      : Sequence<COMPARE_##op##_##type,                                 \
                 I<OPCODE_COMPARE_##op, I8Op, type, type>> {            \
    static void Emit(X64Emitter& e, const EmitArgType& i) {             \
      EmitAssociativeCompareOp(                                         \
          e, i,                                                         \
          [](X64Emitter& e, const Reg8& dest, const reg_type& src1,     \
             const reg_type& src2, bool inverse) {                      \
            e.cmp(src1, src2);                                          \
            if (!inverse) {                                             \
              e.instr(dest);                                            \
            } else {                                                    \
              e.inverse_instr(dest);                                    \
            }                                                           \
          },                                                            \
          [](X64Emitter& e, const Reg8& dest, const reg_type& src1,     \
             int32_t constant, bool inverse) {                          \
            e.cmp(src1, constant);                                      \
            if (!inverse) {                                             \
              e.instr(dest);                                            \
            } else {                                                    \
              e.inverse_instr(dest);                                    \
            }                                                           \
          });                                                           \
    }                                                                   \
  };
#define EMITTER_ASSOCIATIVE_COMPARE_XX(op, instr, inverse_instr)           \
  EMITTER_ASSOCIATIVE_COMPARE_INT(op, instr, inverse_instr, I8Op, Reg8);   \
  EMITTER_ASSOCIATIVE_COMPARE_INT(op, instr, inverse_instr, I16Op, Reg16); \
  EMITTER_ASSOCIATIVE_COMPARE_INT(op, instr, inverse_instr, I32Op, Reg32); \
  EMITTER_ASSOCIATIVE_COMPARE_INT(op, instr, inverse_instr, I64Op, Reg64); \
  EMITTER_OPCODE_TABLE(OPCODE_COMPARE_##op, COMPARE_##op##_I8Op,           \
                       COMPARE_##op##_I16Op, COMPARE_##op##_I32Op,         \
                       COMPARE_##op##_I64Op);
EMITTER_ASSOCIATIVE_COMPARE_XX(SLT, setl, setg);
EMITTER_ASSOCIATIVE_COMPARE_XX(SLE, setle, setge);
EMITTER_ASSOCIATIVE_COMPARE_XX(SGT, setg, setl);
EMITTER_ASSOCIATIVE_COMPARE_XX(SGE, setge, setle);
EMITTER_ASSOCIATIVE_COMPARE_XX(ULT, setb, seta);
EMITTER_ASSOCIATIVE_COMPARE_XX(ULE, setbe, setae);
EMITTER_ASSOCIATIVE_COMPARE_XX(UGT, seta, setb);
EMITTER_ASSOCIATIVE_COMPARE_XX(UGE, setae, setbe);

// http://x86.renejeschke.de/html/file_module_x86_id_288.html
#define EMITTER_ASSOCIATIVE_COMPARE_FLT_XX(op, instr)                 \
  struct COMPARE_##op##_F32                                           \
      : Sequence<COMPARE_##op##_F32,                                  \
                 I<OPCODE_COMPARE_##op, I8Op, F32Op, F32Op>> {        \
    static void Emit(X64Emitter& e, const EmitArgType& i) {           \
      e.vcomiss(i.src1, i.src2);                                      \
      e.instr(i.dest);                                                \
    }                                                                 \
  };                                                                  \
  struct COMPARE_##op##_F64                                           \
      : Sequence<COMPARE_##op##_F64,                                  \
                 I<OPCODE_COMPARE_##op, I8Op, F64Op, F64Op>> {        \
    static void Emit(X64Emitter& e, const EmitArgType& i) {           \
      if (i.src1.is_constant) {                                       \
        e.LoadConstantXmm(e.xmm0, i.src1.constant());                 \
        e.vcomisd(e.xmm0, i.src2);                                    \
      } else if (i.src2.is_constant) {                                \
        e.LoadConstantXmm(e.xmm0, i.src2.constant());                 \
        e.vcomisd(i.src1, e.xmm0);                                    \
      } else {                                                        \
        e.vcomisd(i.src1, i.src2);                                    \
      }                                                               \
      e.instr(i.dest);                                                \
    }                                                                 \
  };                                                                  \
  EMITTER_OPCODE_TABLE(OPCODE_COMPARE_##op##_FLT, COMPARE_##op##_F32, \
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
// OPCODE_DID_SATURATE
// ============================================================================
struct DID_SATURATE
    : Sequence<DID_SATURATE, I<OPCODE_DID_SATURATE, I8Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): implement saturation check (VECTOR_ADD, etc).
    e.xor_(i.dest, i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DID_SATURATE, DID_SATURATE);

// ============================================================================
// OPCODE_VECTOR_COMPARE_EQ
// ============================================================================
struct VECTOR_COMPARE_EQ_V128
    : Sequence<VECTOR_COMPARE_EQ_V128,
               I<OPCODE_VECTOR_COMPARE_EQ, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
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
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_EQ, VECTOR_COMPARE_EQ_V128);

// ============================================================================
// OPCODE_VECTOR_COMPARE_SGT
// ============================================================================
struct VECTOR_COMPARE_SGT_V128
    : Sequence<VECTOR_COMPARE_SGT_V128,
               I<OPCODE_VECTOR_COMPARE_SGT, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAssociativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
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
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_SGT, VECTOR_COMPARE_SGT_V128);

// ============================================================================
// OPCODE_VECTOR_COMPARE_SGE
// ============================================================================
struct VECTOR_COMPARE_SGE_V128
    : Sequence<VECTOR_COMPARE_SGE_V128,
               I<OPCODE_VECTOR_COMPARE_SGE, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAssociativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
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
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_SGE, VECTOR_COMPARE_SGE_V128);

// ============================================================================
// OPCODE_VECTOR_COMPARE_UGT
// ============================================================================
struct VECTOR_COMPARE_UGT_V128
    : Sequence<VECTOR_COMPARE_UGT_V128,
               I<OPCODE_VECTOR_COMPARE_UGT, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Xbyak::Address sign_addr = e.ptr[e.rax];  // dummy
    switch (i.instr->flags) {
      case INT8_TYPE:
        sign_addr = e.GetXmmConstPtr(XMMSignMaskI8);
        break;
      case INT16_TYPE:
        sign_addr = e.GetXmmConstPtr(XMMSignMaskI16);
        break;
      case INT32_TYPE:
        sign_addr = e.GetXmmConstPtr(XMMSignMaskI32);
        break;
      case FLOAT32_TYPE:
        sign_addr = e.GetXmmConstPtr(XMMSignMaskF32);
        break;
      default:
        assert_always();
        break;
    }
    if (i.src1.is_constant) {
      // TODO(benvanik): make this constant.
      e.LoadConstantXmm(e.xmm0, i.src1.constant());
      e.vpxor(e.xmm0, sign_addr);
    } else {
      e.vpxor(e.xmm0, i.src1, sign_addr);
    }
    if (i.src2.is_constant) {
      // TODO(benvanik): make this constant.
      e.LoadConstantXmm(e.xmm1, i.src2.constant());
      e.vpxor(e.xmm1, sign_addr);
    } else {
      e.vpxor(e.xmm1, i.src2, sign_addr);
    }
    switch (i.instr->flags) {
      case INT8_TYPE:
        e.vpcmpgtb(i.dest, e.xmm0, e.xmm1);
        break;
      case INT16_TYPE:
        e.vpcmpgtw(i.dest, e.xmm0, e.xmm1);
        break;
      case INT32_TYPE:
        e.vpcmpgtd(i.dest, e.xmm0, e.xmm1);
        break;
      case FLOAT32_TYPE:
        e.vcmpgtps(i.dest, e.xmm0, e.xmm1);
        break;
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_UGT, VECTOR_COMPARE_UGT_V128);

// ============================================================================
// OPCODE_VECTOR_COMPARE_UGE
// ============================================================================
struct VECTOR_COMPARE_UGE_V128
    : Sequence<VECTOR_COMPARE_UGE_V128,
               I<OPCODE_VECTOR_COMPARE_UGE, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Xbyak::Address sign_addr = e.ptr[e.rax];  // dummy
    switch (i.instr->flags) {
      case INT8_TYPE:
        sign_addr = e.GetXmmConstPtr(XMMSignMaskI8);
        break;
      case INT16_TYPE:
        sign_addr = e.GetXmmConstPtr(XMMSignMaskI16);
        break;
      case INT32_TYPE:
        sign_addr = e.GetXmmConstPtr(XMMSignMaskI32);
        break;
      case FLOAT32_TYPE:
        sign_addr = e.GetXmmConstPtr(XMMSignMaskF32);
        break;
    }
    if (i.src1.is_constant) {
      // TODO(benvanik): make this constant.
      e.LoadConstantXmm(e.xmm0, i.src1.constant());
      e.vpxor(e.xmm0, sign_addr);
    } else {
      e.vpxor(e.xmm0, i.src1, sign_addr);
    }
    if (i.src2.is_constant) {
      // TODO(benvanik): make this constant.
      e.LoadConstantXmm(e.xmm1, i.src2.constant());
      e.vpxor(e.xmm1, sign_addr);
    } else {
      e.vpxor(e.xmm1, i.src2, sign_addr);
    }
    switch (i.instr->flags) {
      case INT8_TYPE:
        e.vpcmpeqb(e.xmm2, e.xmm0, e.xmm1);
        e.vpcmpgtb(i.dest, e.xmm0, e.xmm1);
        e.vpor(i.dest, e.xmm2);
        break;
      case INT16_TYPE:
        e.vpcmpeqw(e.xmm2, e.xmm0, e.xmm1);
        e.vpcmpgtw(i.dest, e.xmm0, e.xmm1);
        e.vpor(i.dest, e.xmm2);
        break;
      case INT32_TYPE:
        e.vpcmpeqd(e.xmm2, e.xmm0, e.xmm1);
        e.vpcmpgtd(i.dest, e.xmm0, e.xmm1);
        e.vpor(i.dest, e.xmm2);
        break;
      case FLOAT32_TYPE:
        e.vcmpgeps(i.dest, e.xmm0, e.xmm1);
        break;
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_COMPARE_UGE, VECTOR_COMPARE_UGE_V128);

// ============================================================================
// OPCODE_ADD
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitAddXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitCommutativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const REG& src) {
        e.add(dest_src, src);
      },
      [](X64Emitter& e, const REG& dest_src, int32_t constant) {
        e.add(dest_src, constant);
      });
}
struct ADD_I8 : Sequence<ADD_I8, I<OPCODE_ADD, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddXX<ADD_I8, Reg8>(e, i);
  }
};
struct ADD_I16 : Sequence<ADD_I16, I<OPCODE_ADD, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddXX<ADD_I16, Reg16>(e, i);
  }
};
struct ADD_I32 : Sequence<ADD_I32, I<OPCODE_ADD, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddXX<ADD_I32, Reg32>(e, i);
  }
};
struct ADD_I64 : Sequence<ADD_I64, I<OPCODE_ADD, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddXX<ADD_I64, Reg64>(e, i);
  }
};
struct ADD_F32 : Sequence<ADD_F32, I<OPCODE_ADD, F32Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vaddss(dest, src1, src2);
                               });
  }
};
struct ADD_F64 : Sequence<ADD_F64, I<OPCODE_ADD, F64Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vaddsd(dest, src1, src2);
                               });
  }
};
struct ADD_V128 : Sequence<ADD_V128, I<OPCODE_ADD, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vaddps(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ADD, ADD_I8, ADD_I16, ADD_I32, ADD_I64, ADD_F32,
                     ADD_F64, ADD_V128);

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
  SEQ::EmitCommutativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const REG& src) {
        e.adc(dest_src, src);
      },
      [](X64Emitter& e, const REG& dest_src, int32_t constant) {
        e.adc(dest_src, constant);
      });
}
struct ADD_CARRY_I8
    : Sequence<ADD_CARRY_I8, I<OPCODE_ADD_CARRY, I8Op, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddCarryXX<ADD_CARRY_I8, Reg8>(e, i);
  }
};
struct ADD_CARRY_I16
    : Sequence<ADD_CARRY_I16, I<OPCODE_ADD_CARRY, I16Op, I16Op, I16Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddCarryXX<ADD_CARRY_I16, Reg16>(e, i);
  }
};
struct ADD_CARRY_I32
    : Sequence<ADD_CARRY_I32, I<OPCODE_ADD_CARRY, I32Op, I32Op, I32Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddCarryXX<ADD_CARRY_I32, Reg32>(e, i);
  }
};
struct ADD_CARRY_I64
    : Sequence<ADD_CARRY_I64, I<OPCODE_ADD_CARRY, I64Op, I64Op, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAddCarryXX<ADD_CARRY_I64, Reg64>(e, i);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ADD_CARRY, ADD_CARRY_I8, ADD_CARRY_I16,
                     ADD_CARRY_I32, ADD_CARRY_I64);

// ============================================================================
// OPCODE_VECTOR_ADD
// ============================================================================
struct VECTOR_ADD
    : Sequence<VECTOR_ADD, I<OPCODE_VECTOR_ADD, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, const Xmm& dest, Xmm src1, Xmm src2) {
          const TypeName part_type =
              static_cast<TypeName>(i.instr->flags & 0xFF);
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
                  // xmm0 is the only temp register that can be used by
                  // src1/src2.
                  e.vpaddd(e.xmm1, src1, src2);

                  // If result is smaller than either of the inputs, we've
                  // overflowed (only need to check one input)
                  // if (src1 > res) then overflowed
                  // http://locklessinc.com/articles/sat_arithmetic/
                  e.vpxor(e.xmm2, src1, e.GetXmmConstPtr(XMMSignMaskI32));
                  e.vpxor(e.xmm0, e.xmm1, e.GetXmmConstPtr(XMMSignMaskI32));
                  e.vpcmpgtd(e.xmm0, e.xmm2, e.xmm0);
                  e.vpor(dest, e.xmm1, e.xmm0);
                } else {
                  e.vpaddd(e.xmm1, src1, src2);

                  // Overflow results if two inputs are the same sign and the
                  // result isn't the same sign. if ((s32b)(~(src1 ^ src2) &
                  // (src1 ^ res)) < 0) then overflowed
                  // http://locklessinc.com/articles/sat_arithmetic/
                  e.vpxor(e.xmm2, src1, src2);
                  e.vpxor(e.xmm3, src1, e.xmm1);
                  e.vpandn(e.xmm2, e.xmm2, e.xmm3);

                  // Set any negative overflowed elements of src1 to INT_MIN
                  e.vpand(e.xmm3, src1, e.xmm2);
                  e.vblendvps(e.xmm1, e.xmm1, e.GetXmmConstPtr(XMMSignMaskI32),
                              e.xmm3);

                  // Set any positive overflowed elements of src1 to INT_MAX
                  e.vpandn(e.xmm3, src1, e.xmm2);
                  e.vblendvps(dest, e.xmm1, e.GetXmmConstPtr(XMMAbsMaskPS),
                              e.xmm3);
                }
              } else {
                e.vpaddd(dest, src1, src2);
              }
              break;
            case FLOAT32_TYPE:
              assert_false(is_unsigned);
              assert_false(saturate);
              e.vaddps(dest, src1, src2);
              break;
            default:
              assert_unhandled_case(part_type);
              break;
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_ADD, VECTOR_ADD);

// ============================================================================
// OPCODE_SUB
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitSubXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitAssociativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const REG& src) {
        e.sub(dest_src, src);
      },
      [](X64Emitter& e, const REG& dest_src, int32_t constant) {
        e.sub(dest_src, constant);
      });
}
struct SUB_I8 : Sequence<SUB_I8, I<OPCODE_SUB, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSubXX<SUB_I8, Reg8>(e, i);
  }
};
struct SUB_I16 : Sequence<SUB_I16, I<OPCODE_SUB, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSubXX<SUB_I16, Reg16>(e, i);
  }
};
struct SUB_I32 : Sequence<SUB_I32, I<OPCODE_SUB, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSubXX<SUB_I32, Reg32>(e, i);
  }
};
struct SUB_I64 : Sequence<SUB_I64, I<OPCODE_SUB, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSubXX<SUB_I64, Reg64>(e, i);
  }
};
struct SUB_F32 : Sequence<SUB_F32, I<OPCODE_SUB, F32Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vsubss(dest, src1, src2);
                               });
  }
};
struct SUB_F64 : Sequence<SUB_F64, I<OPCODE_SUB, F64Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vsubsd(dest, src1, src2);
                               });
  }
};
struct SUB_V128 : Sequence<SUB_V128, I<OPCODE_SUB, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vsubps(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SUB, SUB_I8, SUB_I16, SUB_I32, SUB_I64, SUB_F32,
                     SUB_F64, SUB_V128);

// ============================================================================
// OPCODE_VECTOR_SUB
// ============================================================================
struct VECTOR_SUB
    : Sequence<VECTOR_SUB, I<OPCODE_VECTOR_SUB, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(
        e, i, [&i](X64Emitter& e, const Xmm& dest, Xmm src1, Xmm src2) {
          const TypeName part_type =
              static_cast<TypeName>(i.instr->flags & 0xFF);
          const uint32_t arithmetic_flags = i.instr->flags >> 8;
          bool is_unsigned = !!(arithmetic_flags & ARITHMETIC_UNSIGNED);
          bool saturate = !!(arithmetic_flags & ARITHMETIC_SATURATE);
          switch (part_type) {
            case INT8_TYPE:
              if (saturate) {
                // TODO(benvanik): trace DID_SATURATE
                if (is_unsigned) {
                  e.vpsubusb(dest, src1, src2);
                } else {
                  e.vpsubsb(dest, src1, src2);
                }
              } else {
                e.vpsubb(dest, src1, src2);
              }
              break;
            case INT16_TYPE:
              if (saturate) {
                // TODO(benvanik): trace DID_SATURATE
                if (is_unsigned) {
                  e.vpsubusw(dest, src1, src2);
                } else {
                  e.vpsubsw(dest, src1, src2);
                }
              } else {
                e.vpsubw(dest, src1, src2);
              }
              break;
            case INT32_TYPE:
              if (saturate) {
                if (is_unsigned) {
                  // xmm0 is the only temp register that can be used by
                  // src1/src2.
                  e.vpsubd(e.xmm1, src1, src2);

                  // If result is greater than either of the inputs, we've
                  // underflowed (only need to check one input)
                  // if (res > src1) then underflowed
                  // http://locklessinc.com/articles/sat_arithmetic/
                  e.vpxor(e.xmm2, src1, e.GetXmmConstPtr(XMMSignMaskI32));
                  e.vpxor(e.xmm0, e.xmm1, e.GetXmmConstPtr(XMMSignMaskI32));
                  e.vpcmpgtd(e.xmm0, e.xmm0, e.xmm2);
                  e.vpandn(dest, e.xmm0, e.xmm1);
                } else {
                  e.vpsubd(e.xmm1, src1, src2);

                  // We can only overflow if the signs of the operands are
                  // opposite. If signs are opposite and result sign isn't the
                  // same as src1's sign, we've overflowed. if ((s32b)((src1 ^
                  // src2) & (src1 ^ res)) < 0) then overflowed
                  // http://locklessinc.com/articles/sat_arithmetic/
                  e.vpxor(e.xmm2, src1, src2);
                  e.vpxor(e.xmm3, src1, e.xmm1);
                  e.vpand(e.xmm2, e.xmm2, e.xmm3);

                  // Set any negative overflowed elements of src1 to INT_MIN
                  e.vpand(e.xmm3, src1, e.xmm2);
                  e.vblendvps(e.xmm1, e.xmm1, e.GetXmmConstPtr(XMMSignMaskI32),
                              e.xmm3);

                  // Set any positive overflowed elements of src1 to INT_MAX
                  e.vpandn(e.xmm3, src1, e.xmm2);
                  e.vblendvps(dest, e.xmm1, e.GetXmmConstPtr(XMMAbsMaskPS),
                              e.xmm3);
                }
              } else {
                e.vpsubd(dest, src1, src2);
              }
              break;
            case FLOAT32_TYPE:
              e.vsubps(dest, src1, src2);
              break;
            default:
              assert_unhandled_case(part_type);
              break;
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SUB, VECTOR_SUB);

// ============================================================================
// OPCODE_MUL
// ============================================================================
// Sign doesn't matter here, as we don't use the high bits.
// We exploit mulx here to avoid creating too much register pressure.
struct MUL_I8 : Sequence<MUL_I8, I<OPCODE_MUL, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (e.IsFeatureEnabled(kX64EmitBMI2)) {
      // mulx: $1:$2 = EDX * $3

      // TODO(benvanik): place src2 in edx?
      if (i.src1.is_constant) {
        assert_true(!i.src2.is_constant);
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
    } else {
      // x86 mul instruction
      // AH:AL = AL * $1;

      if (i.src1.is_constant) {
        assert_true(!i.src2.is_constant);
        e.mov(e.al, i.src1.constant());
        e.mul(i.src2);
        e.mov(i.dest, e.al);
      } else if (i.src2.is_constant) {
        assert_true(!i.src1.is_constant);
        e.mov(e.al, i.src2.constant());
        e.mul(i.src1);
        e.mov(i.dest, e.al);
      } else {
        e.movzx(e.al, i.src1);
        e.mul(i.src2);
        e.mov(i.dest, e.al);
      }
    }
  }
};
struct MUL_I16 : Sequence<MUL_I16, I<OPCODE_MUL, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (e.IsFeatureEnabled(kX64EmitBMI2)) {
      // mulx: $1:$2 = EDX * $3

      // TODO(benvanik): place src2 in edx?
      if (i.src1.is_constant) {
        assert_true(!i.src2.is_constant);
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
    } else {
      // x86 mul instruction
      // DX:AX = AX * $1;

      if (i.src1.is_constant) {
        assert_true(!i.src2.is_constant);
        e.mov(e.ax, i.src1.constant());
        e.mul(i.src2);
        e.movzx(i.dest, e.ax);
      } else if (i.src2.is_constant) {
        assert_true(!i.src1.is_constant);
        e.mov(e.ax, i.src2.constant());
        e.mul(i.src1);
        e.movzx(i.dest, e.ax);
      } else {
        e.movzx(e.ax, i.src1);
        e.mul(i.src2);
        e.movzx(i.dest, e.ax);
      }
    }
  }
};
struct MUL_I32 : Sequence<MUL_I32, I<OPCODE_MUL, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (e.IsFeatureEnabled(kX64EmitBMI2)) {
      // mulx: $1:$2 = EDX * $3

      // TODO(benvanik): place src2 in edx?
      if (i.src1.is_constant) {
        assert_true(!i.src2.is_constant);
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
    } else {
      // x86 mul instruction
      // EDX:EAX = EAX * $1;

      // is_constant AKA not a register
      if (i.src1.is_constant) {
        assert_true(!i.src2.is_constant);  // can't multiply 2 constants
        e.mov(e.eax, i.src1.constant());
        e.mul(i.src2);
        e.mov(i.dest, e.eax);
      } else if (i.src2.is_constant) {
        assert_true(!i.src1.is_constant);  // can't multiply 2 constants
        e.mov(e.eax, i.src2.constant());
        e.mul(i.src1);
        e.mov(i.dest, e.eax);
      } else {
        e.mov(e.eax, i.src1);
        e.mul(i.src2);
        e.mov(i.dest, e.eax);
      }
    }
  }
};
struct MUL_I64 : Sequence<MUL_I64, I<OPCODE_MUL, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (e.IsFeatureEnabled(kX64EmitBMI2)) {
      // mulx: $1:$2 = RDX * $3

      // TODO(benvanik): place src2 in edx?
      if (i.src1.is_constant) {
        assert_true(!i.src2.is_constant);
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
    } else {
      // x86 mul instruction
      // RDX:RAX = RAX * $1;

      if (i.src1.is_constant) {
        assert_true(!i.src2.is_constant);  // can't multiply 2 constants
        e.mov(e.rax, i.src1.constant());
        e.mul(i.src2);
        e.mov(i.dest, e.rax);
      } else if (i.src2.is_constant) {
        assert_true(!i.src1.is_constant);  // can't multiply 2 constants
        e.mov(e.rax, i.src2.constant());
        e.mul(i.src1);
        e.mov(i.dest, e.rax);
      } else {
        e.mov(e.rax, i.src1);
        e.mul(i.src2);
        e.mov(i.dest, e.rax);
      }
    }
  }
};
struct MUL_F32 : Sequence<MUL_F32, I<OPCODE_MUL, F32Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vmulss(dest, src1, src2);
                               });
  }
};
struct MUL_F64 : Sequence<MUL_F64, I<OPCODE_MUL, F64Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vmulsd(dest, src1, src2);
                               });
  }
};
struct MUL_V128 : Sequence<MUL_V128, I<OPCODE_MUL, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vmulps(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL, MUL_I8, MUL_I16, MUL_I32, MUL_I64, MUL_F32,
                     MUL_F64, MUL_V128);

// ============================================================================
// OPCODE_MUL_HI
// ============================================================================
struct MUL_HI_I8 : Sequence<MUL_HI_I8, I<OPCODE_MUL_HI, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      // mulx: $1:$2 = EDX * $3

      if (e.IsFeatureEnabled(kX64EmitBMI2)) {
        // TODO(benvanik): place src1 in eax? still need to sign extend
        e.movzx(e.edx, i.src1);
        e.mulx(i.dest.reg().cvt32(), e.eax, i.src2.reg().cvt32());
      } else {
        // x86 mul instruction
        // AH:AL = AL * $1;
        if (i.src1.is_constant) {
          assert_true(!i.src2.is_constant);  // can't multiply 2 constants
          e.mov(e.al, i.src1.constant());
          e.mul(i.src2);
          e.mov(i.dest, e.ah);
        } else if (i.src2.is_constant) {
          assert_true(!i.src1.is_constant);  // can't multiply 2 constants
          e.mov(e.al, i.src2.constant());
          e.mul(i.src1);
          e.mov(i.dest, e.ah);
        } else {
          e.mov(e.al, i.src1);
          e.mul(i.src2);
          e.mov(i.dest, e.ah);
        }
      }
    } else {
      if (i.src1.is_constant) {
        e.mov(e.al, i.src1.constant());
      } else {
        e.mov(e.al, i.src1);
      }
      if (i.src2.is_constant) {
        e.mov(e.al, i.src2.constant());
        e.imul(e.al);
      } else {
        e.imul(i.src2);
      }
      e.mov(i.dest, e.ah);
    }
  }
};
struct MUL_HI_I16
    : Sequence<MUL_HI_I16, I<OPCODE_MUL_HI, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      if (e.IsFeatureEnabled(kX64EmitBMI2)) {
        // TODO(benvanik): place src1 in eax? still need to sign extend
        e.movzx(e.edx, i.src1);
        e.mulx(i.dest.reg().cvt32(), e.eax, i.src2.reg().cvt32());
      } else {
        // x86 mul instruction
        // DX:AX = AX * $1;
        if (i.src1.is_constant) {
          assert_true(!i.src2.is_constant);  // can't multiply 2 constants
          e.mov(e.ax, i.src1.constant());
          e.mul(i.src2);
          e.mov(i.dest, e.dx);
        } else if (i.src2.is_constant) {
          assert_true(!i.src1.is_constant);  // can't multiply 2 constants
          e.mov(e.ax, i.src2.constant());
          e.mul(i.src1);
          e.mov(i.dest, e.dx);
        } else {
          e.mov(e.ax, i.src1);
          e.mul(i.src2);
          e.mov(i.dest, e.dx);
        }
      }
    } else {
      if (i.src1.is_constant) {
        e.mov(e.ax, i.src1.constant());
      } else {
        e.mov(e.ax, i.src1);
      }
      if (i.src2.is_constant) {
        e.mov(e.dx, i.src2.constant());
        e.imul(e.dx);
      } else {
        e.imul(i.src2);
      }
      e.mov(i.dest, e.dx);
    }
  }
};
struct MUL_HI_I32
    : Sequence<MUL_HI_I32, I<OPCODE_MUL_HI, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      if (e.IsFeatureEnabled(kX64EmitBMI2)) {
        // TODO(benvanik): place src1 in eax? still need to sign extend
        e.mov(e.edx, i.src1);
        if (i.src2.is_constant) {
          e.mov(e.eax, i.src2.constant());
          e.mulx(i.dest, e.edx, e.eax);
        } else {
          e.mulx(i.dest, e.edx, i.src2);
        }
      } else {
        // x86 mul instruction
        // EDX:EAX = EAX * $1;
        if (i.src1.is_constant) {
          assert_true(!i.src2.is_constant);  // can't multiply 2 constants
          e.mov(e.eax, i.src1.constant());
          e.mul(i.src2);
          e.mov(i.dest, e.edx);
        } else if (i.src2.is_constant) {
          assert_true(!i.src1.is_constant);  // can't multiply 2 constants
          e.mov(e.eax, i.src2.constant());
          e.mul(i.src1);
          e.mov(i.dest, e.edx);
        } else {
          e.mov(e.eax, i.src1);
          e.mul(i.src2);
          e.mov(i.dest, e.edx);
        }
      }
    } else {
      if (i.src1.is_constant) {
        e.mov(e.eax, i.src1.constant());
      } else {
        e.mov(e.eax, i.src1);
      }
      if (i.src2.is_constant) {
        e.mov(e.edx, i.src2.constant());
        e.imul(e.edx);
      } else {
        e.imul(i.src2);
      }
      e.mov(i.dest, e.edx);
    }
  }
};
struct MUL_HI_I64
    : Sequence<MUL_HI_I64, I<OPCODE_MUL_HI, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      if (e.IsFeatureEnabled(kX64EmitBMI2)) {
        // TODO(benvanik): place src1 in eax? still need to sign extend
        e.mov(e.rdx, i.src1);
        if (i.src2.is_constant) {
          e.mov(e.rax, i.src2.constant());
          e.mulx(i.dest, e.rdx, e.rax);
        } else {
          e.mulx(i.dest, e.rax, i.src2);
        }
      } else {
        // x86 mul instruction
        // RDX:RAX < RAX * REG(op1);
        if (i.src1.is_constant) {
          assert_true(!i.src2.is_constant);  // can't multiply 2 constants
          e.mov(e.rax, i.src1.constant());
          e.mul(i.src2);
          e.mov(i.dest, e.rdx);
        } else if (i.src2.is_constant) {
          assert_true(!i.src1.is_constant);  // can't multiply 2 constants
          e.mov(e.rax, i.src2.constant());
          e.mul(i.src1);
          e.mov(i.dest, e.rdx);
        } else {
          e.mov(e.rax, i.src1);
          e.mul(i.src2);
          e.mov(i.dest, e.rdx);
        }
      }
    } else {
      if (i.src1.is_constant) {
        e.mov(e.rax, i.src1.constant());
      } else {
        e.mov(e.rax, i.src1);
      }
      if (i.src2.is_constant) {
        e.mov(e.rdx, i.src2.constant());
        e.imul(e.rdx);
      } else {
        e.imul(i.src2);
      }
      e.mov(i.dest, e.rdx);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_HI, MUL_HI_I8, MUL_HI_I16, MUL_HI_I32,
                     MUL_HI_I64);

// ============================================================================
// OPCODE_DIV
// ============================================================================
// TODO(benvanik): optimize common constant cases.
// TODO(benvanik): simplify code!
struct DIV_I8 : Sequence<DIV_I8, I<OPCODE_DIV, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Xbyak::Label skip;
    e.inLocalLabel();

    if (i.src2.is_constant) {
      assert_true(!i.src1.is_constant);
      e.mov(e.cl, i.src2.constant());
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        e.movzx(e.ax, i.src1);
        e.div(e.cl);
      } else {
        e.movsx(e.ax, i.src1);
        e.idiv(e.cl);
      }
    } else {
      // Skip if src2 is zero.
      e.test(i.src2, i.src2);
      e.jz(skip, CodeGenerator::T_SHORT);

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

    e.L(skip);
    e.outLocalLabel();
    e.mov(i.dest, e.al);
  }
};
struct DIV_I16 : Sequence<DIV_I16, I<OPCODE_DIV, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Xbyak::Label skip;
    e.inLocalLabel();

    if (i.src2.is_constant) {
      assert_true(!i.src1.is_constant);
      e.mov(e.cx, i.src2.constant());
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        e.mov(e.ax, i.src1);
        // Zero upper bits.
        e.xor_(e.dx, e.dx);
        e.div(e.cx);
      } else {
        e.mov(e.ax, i.src1);
        e.cwd();  // dx:ax = sign-extend ax
        e.idiv(e.cx);
      }
    } else {
      // Skip if src2 is zero.
      e.test(i.src2, i.src2);
      e.jz(skip, CodeGenerator::T_SHORT);

      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        if (i.src1.is_constant) {
          e.mov(e.ax, i.src1.constant());
        } else {
          e.mov(e.ax, i.src1);
        }
        // Zero upper bits.
        e.xor_(e.dx, e.dx);
        e.div(i.src2);
      } else {
        if (i.src1.is_constant) {
          e.mov(e.ax, i.src1.constant());
        } else {
          e.mov(e.ax, i.src1);
        }
        e.cwd();  // dx:ax = sign-extend ax
        e.idiv(i.src2);
      }
    }

    e.L(skip);
    e.outLocalLabel();
    e.mov(i.dest, e.ax);
  }
};
struct DIV_I32 : Sequence<DIV_I32, I<OPCODE_DIV, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Xbyak::Label skip;
    e.inLocalLabel();

    if (i.src2.is_constant) {
      assert_true(!i.src1.is_constant);
      e.mov(e.ecx, i.src2.constant());
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        e.mov(e.eax, i.src1);
        // Zero upper bits.
        e.xor_(e.edx, e.edx);
        e.div(e.ecx);
      } else {
        e.mov(e.eax, i.src1);
        e.cdq();  // edx:eax = sign-extend eax
        e.idiv(e.ecx);
      }
    } else {
      // Skip if src2 is zero.
      e.test(i.src2, i.src2);
      e.jz(skip, CodeGenerator::T_SHORT);

      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        if (i.src1.is_constant) {
          e.mov(e.eax, i.src1.constant());
        } else {
          e.mov(e.eax, i.src1);
        }
        // Zero upper bits.
        e.xor_(e.edx, e.edx);
        e.div(i.src2);
      } else {
        if (i.src1.is_constant) {
          e.mov(e.eax, i.src1.constant());
        } else {
          e.mov(e.eax, i.src1);
        }
        e.cdq();  // edx:eax = sign-extend eax
        e.idiv(i.src2);
      }
    }

    e.L(skip);
    e.outLocalLabel();
    e.mov(i.dest, e.eax);
  }
};
struct DIV_I64 : Sequence<DIV_I64, I<OPCODE_DIV, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    Xbyak::Label skip;
    e.inLocalLabel();

    if (i.src2.is_constant) {
      assert_true(!i.src1.is_constant);
      e.mov(e.rcx, i.src2.constant());
      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        e.mov(e.rax, i.src1);
        // Zero upper bits.
        e.xor_(e.rdx, e.rdx);
        e.div(e.rcx);
      } else {
        e.mov(e.rax, i.src1);
        e.cqo();  // rdx:rax = sign-extend rax
        e.idiv(e.rcx);
      }
    } else {
      // Skip if src2 is zero.
      e.test(i.src2, i.src2);
      e.jz(skip, CodeGenerator::T_SHORT);

      if (i.instr->flags & ARITHMETIC_UNSIGNED) {
        if (i.src1.is_constant) {
          e.mov(e.rax, i.src1.constant());
        } else {
          e.mov(e.rax, i.src1);
        }
        // Zero upper bits.
        e.xor_(e.rdx, e.rdx);
        e.div(i.src2);
      } else {
        if (i.src1.is_constant) {
          e.mov(e.rax, i.src1.constant());
        } else {
          e.mov(e.rax, i.src1);
        }
        e.cqo();  // rdx:rax = sign-extend rax
        e.idiv(i.src2);
      }
    }

    e.L(skip);
    e.outLocalLabel();
    e.mov(i.dest, e.rax);
  }
};
struct DIV_F32 : Sequence<DIV_F32, I<OPCODE_DIV, F32Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vdivss(dest, src1, src2);
                               });
  }
};
struct DIV_F64 : Sequence<DIV_F64, I<OPCODE_DIV, F64Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vdivsd(dest, src1, src2);
                               });
  }
};
struct DIV_V128 : Sequence<DIV_V128, I<OPCODE_DIV, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    EmitAssociativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vdivps(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DIV, DIV_I8, DIV_I16, DIV_I32, DIV_I64, DIV_F32,
                     DIV_F64, DIV_V128);

// ============================================================================
// OPCODE_MUL_ADD
// ============================================================================
// d = 1 * 2 + 3
// $0 = $1x$0 + $2
// Forms of vfmadd/vfmsub:
// - 132 -> $1 = $1 * $3 + $2
// - 213 -> $1 = $2 * $1 + $3
// - 231 -> $1 = $2 * $3 + $1
struct MUL_ADD_F32
    : Sequence<MUL_ADD_F32, I<OPCODE_MUL_ADD, F32Op, F32Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // FMA extension
    if (e.IsFeatureEnabled(kX64EmitFMA)) {
      EmitCommutativeBinaryXmmOp(e, i,
                                 [&i](X64Emitter& e, const Xmm& dest,
                                      const Xmm& src1, const Xmm& src2) {
                                   Xmm src3 =
                                       i.src3.is_constant ? e.xmm1 : i.src3;
                                   if (i.src3.is_constant) {
                                     e.LoadConstantXmm(src3, i.src3.constant());
                                   }
                                   if (i.dest == src1) {
                                     e.vfmadd213ss(i.dest, src2, src3);
                                   } else if (i.dest == src2) {
                                     e.vfmadd213ss(i.dest, src1, src3);
                                   } else if (i.dest == i.src3) {
                                     e.vfmadd231ss(i.dest, src1, src2);
                                   } else {
                                     // Dest not equal to anything
                                     e.vmovss(i.dest, src1);
                                     e.vfmadd213ss(i.dest, src2, src3);
                                   }
                                 });
    } else {
      Xmm src3;
      if (i.src3.is_constant) {
        src3 = e.xmm1;
        e.LoadConstantXmm(src3, i.src3.constant());
      } else {
        // If i.dest == i.src3, back up i.src3 so we don't overwrite it.
        src3 = i.src3;
        if (i.dest == i.src3) {
          e.vmovss(e.xmm1, i.src3);
          src3 = e.xmm1;
        }
      }

      // Multiply operation is commutative.
      EmitCommutativeBinaryXmmOp(
          e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
            e.vmulss(dest, src1, src2);  // $0 = $1 * $2
          });

      e.vaddss(i.dest, i.dest, src3);  // $0 = $1 + $2
    }
  }
};
struct MUL_ADD_F64
    : Sequence<MUL_ADD_F64, I<OPCODE_MUL_ADD, F64Op, F64Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // FMA extension
    if (e.IsFeatureEnabled(kX64EmitFMA)) {
      EmitCommutativeBinaryXmmOp(e, i,
                                 [&i](X64Emitter& e, const Xmm& dest,
                                      const Xmm& src1, const Xmm& src2) {
                                   Xmm src3 =
                                       i.src3.is_constant ? e.xmm1 : i.src3;
                                   if (i.src3.is_constant) {
                                     e.LoadConstantXmm(src3, i.src3.constant());
                                   }
                                   if (i.dest == src1) {
                                     e.vfmadd213sd(i.dest, src2, src3);
                                   } else if (i.dest == src2) {
                                     e.vfmadd213sd(i.dest, src1, src3);
                                   } else if (i.dest == i.src3) {
                                     e.vfmadd231sd(i.dest, src1, src2);
                                   } else {
                                     // Dest not equal to anything
                                     e.vmovsd(i.dest, src1);
                                     e.vfmadd213sd(i.dest, src2, src3);
                                   }
                                 });
    } else {
      Xmm src3;
      if (i.src3.is_constant) {
        src3 = e.xmm1;
        e.LoadConstantXmm(src3, i.src3.constant());
      } else {
        // If i.dest == i.src3, back up i.src3 so we don't overwrite it.
        src3 = i.src3;
        if (i.dest == i.src3) {
          e.vmovsd(e.xmm1, i.src3);
          src3 = e.xmm1;
        }
      }

      // Multiply operation is commutative.
      EmitCommutativeBinaryXmmOp(
          e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
            e.vmulsd(dest, src1, src2);  // $0 = $1 * $2
          });

      e.vaddsd(i.dest, i.dest, src3);  // $0 = $1 + $2
    }
  }
};
struct MUL_ADD_V128
    : Sequence<MUL_ADD_V128,
               I<OPCODE_MUL_ADD, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): the vfmadd sequence produces slightly different results
    // than vmul+vadd and it'd be nice to know why. Until we know, it's
    // disabled so tests pass.
    if (false && e.IsFeatureEnabled(kX64EmitFMA)) {
      EmitCommutativeBinaryXmmOp(e, i,
                                 [&i](X64Emitter& e, const Xmm& dest,
                                      const Xmm& src1, const Xmm& src2) {
                                   Xmm src3 =
                                       i.src3.is_constant ? e.xmm1 : i.src3;
                                   if (i.src3.is_constant) {
                                     e.LoadConstantXmm(src3, i.src3.constant());
                                   }
                                   if (i.dest == src1) {
                                     e.vfmadd213ps(i.dest, src2, src3);
                                   } else if (i.dest == src2) {
                                     e.vfmadd213ps(i.dest, src1, src3);
                                   } else if (i.dest == i.src3) {
                                     e.vfmadd231ps(i.dest, src1, src2);
                                   } else {
                                     // Dest not equal to anything
                                     e.vmovdqa(i.dest, src1);
                                     e.vfmadd213ps(i.dest, src2, src3);
                                   }
                                 });
    } else {
      Xmm src3;
      if (i.src3.is_constant) {
        src3 = e.xmm1;
        e.LoadConstantXmm(src3, i.src3.constant());
      } else {
        // If i.dest == i.src3, back up i.src3 so we don't overwrite it.
        src3 = i.src3;
        if (i.dest == i.src3) {
          e.vmovdqa(e.xmm1, i.src3);
          src3 = e.xmm1;
        }
      }

      // Multiply operation is commutative.
      EmitCommutativeBinaryXmmOp(
          e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
            e.vmulps(dest, src1, src2);  // $0 = $1 * $2
          });

      e.vaddps(i.dest, i.dest, src3);  // $0 = $1 + $2
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_ADD, MUL_ADD_F32, MUL_ADD_F64, MUL_ADD_V128);

// ============================================================================
// OPCODE_MUL_SUB
// ============================================================================
// d = 1 * 2 - 3
// $0 = $2x$0 - $3
// TODO(benvanik): use other forms (132/213/etc) to avoid register shuffling.
// dest could be src2 or src3 - need to ensure it's not before overwriting dest
// perhaps use other 132/213/etc
// Forms:
// - 132 -> $1 = $1 * $3 - $2
// - 213 -> $1 = $2 * $1 - $3
// - 231 -> $1 = $2 * $3 - $1
struct MUL_SUB_F32
    : Sequence<MUL_SUB_F32, I<OPCODE_MUL_SUB, F32Op, F32Op, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // FMA extension
    if (e.IsFeatureEnabled(kX64EmitFMA)) {
      EmitCommutativeBinaryXmmOp(e, i,
                                 [&i](X64Emitter& e, const Xmm& dest,
                                      const Xmm& src1, const Xmm& src2) {
                                   Xmm src3 =
                                       i.src3.is_constant ? e.xmm1 : i.src3;
                                   if (i.src3.is_constant) {
                                     e.LoadConstantXmm(src3, i.src3.constant());
                                   }
                                   if (i.dest == src1) {
                                     e.vfmsub213ss(i.dest, src2, src3);
                                   } else if (i.dest == src2) {
                                     e.vfmsub213ss(i.dest, src1, src3);
                                   } else if (i.dest == i.src3) {
                                     e.vfmsub231ss(i.dest, src1, src2);
                                   } else {
                                     // Dest not equal to anything
                                     e.vmovss(i.dest, src1);
                                     e.vfmsub213ss(i.dest, src2, src3);
                                   }
                                 });
    } else {
      Xmm src3;
      if (i.src3.is_constant) {
        src3 = e.xmm1;
        e.LoadConstantXmm(src3, i.src3.constant());
      } else {
        // If i.dest == i.src3, back up i.src3 so we don't overwrite it.
        src3 = i.src3;
        if (i.dest == i.src3) {
          e.vmovss(e.xmm1, i.src3);
          src3 = e.xmm1;
        }
      }

      // Multiply operation is commutative.
      EmitCommutativeBinaryXmmOp(
          e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
            e.vmulss(dest, src1, src2);  // $0 = $1 * $2
          });

      e.vsubss(i.dest, i.dest, src3);  // $0 = $1 - $2
    }
  }
};
struct MUL_SUB_F64
    : Sequence<MUL_SUB_F64, I<OPCODE_MUL_SUB, F64Op, F64Op, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // FMA extension
    if (e.IsFeatureEnabled(kX64EmitFMA)) {
      EmitCommutativeBinaryXmmOp(e, i,
                                 [&i](X64Emitter& e, const Xmm& dest,
                                      const Xmm& src1, const Xmm& src2) {
                                   Xmm src3 =
                                       i.src3.is_constant ? e.xmm1 : i.src3;
                                   if (i.src3.is_constant) {
                                     e.LoadConstantXmm(src3, i.src3.constant());
                                   }
                                   if (i.dest == src1) {
                                     e.vfmsub213sd(i.dest, src2, src3);
                                   } else if (i.dest == src2) {
                                     e.vfmsub213sd(i.dest, src1, src3);
                                   } else if (i.dest == i.src3) {
                                     e.vfmsub231sd(i.dest, src1, src2);
                                   } else {
                                     // Dest not equal to anything
                                     e.vmovsd(i.dest, src1);
                                     e.vfmsub213sd(i.dest, src2, src3);
                                   }
                                 });
    } else {
      Xmm src3;
      if (i.src3.is_constant) {
        src3 = e.xmm1;
        e.LoadConstantXmm(src3, i.src3.constant());
      } else {
        // If i.dest == i.src3, back up i.src3 so we don't overwrite it.
        src3 = i.src3;
        if (i.dest == i.src3) {
          e.vmovsd(e.xmm1, i.src3);
          src3 = e.xmm1;
        }
      }

      // Multiply operation is commutative.
      EmitCommutativeBinaryXmmOp(
          e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
            e.vmulsd(dest, src1, src2);  // $0 = $1 * $2
          });

      e.vsubsd(i.dest, i.dest, src3);  // $0 = $1 - $2
    }
  }
};
struct MUL_SUB_V128
    : Sequence<MUL_SUB_V128,
               I<OPCODE_MUL_SUB, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // FMA extension
    if (e.IsFeatureEnabled(kX64EmitFMA)) {
      EmitCommutativeBinaryXmmOp(e, i,
                                 [&i](X64Emitter& e, const Xmm& dest,
                                      const Xmm& src1, const Xmm& src2) {
                                   Xmm src3 =
                                       i.src3.is_constant ? e.xmm1 : i.src3;
                                   if (i.src3.is_constant) {
                                     e.LoadConstantXmm(src3, i.src3.constant());
                                   }
                                   if (i.dest == src1) {
                                     e.vfmsub213ps(i.dest, src2, src3);
                                   } else if (i.dest == src2) {
                                     e.vfmsub213ps(i.dest, src1, src3);
                                   } else if (i.dest == i.src3) {
                                     e.vfmsub231ps(i.dest, src1, src2);
                                   } else {
                                     // Dest not equal to anything
                                     e.vmovdqa(i.dest, src1);
                                     e.vfmsub213ps(i.dest, src2, src3);
                                   }
                                 });
    } else {
      Xmm src3;
      if (i.src3.is_constant) {
        src3 = e.xmm1;
        e.LoadConstantXmm(src3, i.src3.constant());
      } else {
        // If i.dest == i.src3, back up i.src3 so we don't overwrite it.
        src3 = i.src3;
        if (i.dest == i.src3) {
          e.vmovdqa(e.xmm1, i.src3);
          src3 = e.xmm1;
        }
      }

      // Multiply operation is commutative.
      EmitCommutativeBinaryXmmOp(
          e, i, [&i](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
            e.vmulps(dest, src1, src2);  // $0 = $1 * $2
          });

      e.vsubps(i.dest, i.dest, src3);  // $0 = $1 - $2
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_SUB, MUL_SUB_F32, MUL_SUB_F64, MUL_SUB_V128);

// ============================================================================
// OPCODE_NEG
// ============================================================================
// TODO(benvanik): put dest/src1 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitNegXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitUnaryOp(e, i,
                   [](X64Emitter& e, const REG& dest_src) { e.neg(dest_src); });
}
struct NEG_I8 : Sequence<NEG_I8, I<OPCODE_NEG, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNegXX<NEG_I8, Reg8>(e, i);
  }
};
struct NEG_I16 : Sequence<NEG_I16, I<OPCODE_NEG, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNegXX<NEG_I16, Reg16>(e, i);
  }
};
struct NEG_I32 : Sequence<NEG_I32, I<OPCODE_NEG, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNegXX<NEG_I32, Reg32>(e, i);
  }
};
struct NEG_I64 : Sequence<NEG_I64, I<OPCODE_NEG, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNegXX<NEG_I64, Reg64>(e, i);
  }
};
struct NEG_F32 : Sequence<NEG_F32, I<OPCODE_NEG, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vxorps(i.dest, i.src1, e.GetXmmConstPtr(XMMSignMaskPS));
  }
};
struct NEG_F64 : Sequence<NEG_F64, I<OPCODE_NEG, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vxorpd(i.dest, i.src1, e.GetXmmConstPtr(XMMSignMaskPD));
  }
};
struct NEG_V128 : Sequence<NEG_V128, I<OPCODE_NEG, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(!i.instr->flags);
    e.vxorps(i.dest, i.src1, e.GetXmmConstPtr(XMMSignMaskPS));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_NEG, NEG_I8, NEG_I16, NEG_I32, NEG_I64, NEG_F32,
                     NEG_F64, NEG_V128);

// ============================================================================
// OPCODE_ABS
// ============================================================================
struct ABS_F32 : Sequence<ABS_F32, I<OPCODE_ABS, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vpand(i.dest, i.src1, e.GetXmmConstPtr(XMMAbsMaskPS));
  }
};
struct ABS_F64 : Sequence<ABS_F64, I<OPCODE_ABS, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vpand(i.dest, i.src1, e.GetXmmConstPtr(XMMAbsMaskPD));
  }
};
struct ABS_V128 : Sequence<ABS_V128, I<OPCODE_ABS, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vpand(i.dest, i.src1, e.GetXmmConstPtr(XMMAbsMaskPS));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ABS, ABS_F32, ABS_F64, ABS_V128);

// ============================================================================
// OPCODE_SQRT
// ============================================================================
struct SQRT_F32 : Sequence<SQRT_F32, I<OPCODE_SQRT, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vsqrtss(i.dest, i.src1);
  }
};
struct SQRT_F64 : Sequence<SQRT_F64, I<OPCODE_SQRT, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vsqrtsd(i.dest, i.src1);
  }
};
struct SQRT_V128 : Sequence<SQRT_V128, I<OPCODE_SQRT, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vsqrtps(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SQRT, SQRT_F32, SQRT_F64, SQRT_V128);

// ============================================================================
// OPCODE_RSQRT
// ============================================================================
struct RSQRT_F32 : Sequence<RSQRT_F32, I<OPCODE_RSQRT, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vrsqrtss(i.dest, i.src1);
  }
};
struct RSQRT_F64 : Sequence<RSQRT_F64, I<OPCODE_RSQRT, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vcvtsd2ss(i.dest, i.src1);
    e.vrsqrtss(i.dest, i.dest);
    e.vcvtss2sd(i.dest, i.dest);
  }
};
struct RSQRT_V128 : Sequence<RSQRT_V128, I<OPCODE_RSQRT, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vrsqrtps(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_RSQRT, RSQRT_F32, RSQRT_F64, RSQRT_V128);

// ============================================================================
// OPCODE_RECIP
// ============================================================================
struct RECIP_F32 : Sequence<RECIP_F32, I<OPCODE_RECIP, F32Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vrcpss(i.dest, i.src1);
  }
};
struct RECIP_F64 : Sequence<RECIP_F64, I<OPCODE_RECIP, F64Op, F64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vcvtsd2ss(i.dest, i.src1);
    e.vrcpss(i.dest, i.dest);
    e.vcvtss2sd(i.dest, i.dest);
  }
};
struct RECIP_V128 : Sequence<RECIP_V128, I<OPCODE_RECIP, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.vrcpps(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_RECIP, RECIP_F32, RECIP_F64, RECIP_V128);

// ============================================================================
// OPCODE_POW2
// ============================================================================
// TODO(benvanik): use approx here:
//     http://jrfonseca.blogspot.com/2008/09/fast-sse2-pow-tables-or-polynomials.html
struct POW2_F32 : Sequence<POW2_F32, I<OPCODE_POW2, F32Op, F32Op>> {
  static __m128 EmulatePow2(void*, __m128 src) {
    float src_value;
    _mm_store_ss(&src_value, src);
    float result = std::exp2(src_value);
    return _mm_load_ss(&result);
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_always();
    e.lea(e.r8, e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulatePow2));
    e.vmovaps(i.dest, e.xmm0);
  }
};
struct POW2_F64 : Sequence<POW2_F64, I<OPCODE_POW2, F64Op, F64Op>> {
  static __m128d EmulatePow2(void*, __m128d src) {
    double src_value;
    _mm_store_sd(&src_value, src);
    double result = std::exp2(src_value);
    return _mm_load_sd(&result);
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_always();
    e.lea(e.r8, e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulatePow2));
    e.vmovaps(i.dest, e.xmm0);
  }
};
struct POW2_V128 : Sequence<POW2_V128, I<OPCODE_POW2, V128Op, V128Op>> {
  static __m128 EmulatePow2(void*, __m128 src) {
    alignas(16) float values[4];
    _mm_store_ps(values, src);
    for (size_t i = 0; i < 4; ++i) {
      values[i] = std::exp2(values[i]);
    }
    return _mm_load_ps(values);
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.lea(e.r8, e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulatePow2));
    e.vmovaps(i.dest, e.xmm0);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_POW2, POW2_F32, POW2_F64, POW2_V128);

// ============================================================================
// OPCODE_LOG2
// ============================================================================
// TODO(benvanik): use approx here:
//     http://jrfonseca.blogspot.com/2008/09/fast-sse2-pow-tables-or-polynomials.html
// TODO(benvanik): this emulated fn destroys all xmm registers! don't do it!
struct LOG2_F32 : Sequence<LOG2_F32, I<OPCODE_LOG2, F32Op, F32Op>> {
  static __m128 EmulateLog2(void*, __m128 src) {
    float src_value;
    _mm_store_ss(&src_value, src);
    float result = std::log2(src_value);
    return _mm_load_ss(&result);
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_always();
    e.lea(e.r8, e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateLog2));
    e.vmovaps(i.dest, e.xmm0);
  }
};
struct LOG2_F64 : Sequence<LOG2_F64, I<OPCODE_LOG2, F64Op, F64Op>> {
  static __m128d EmulateLog2(void*, __m128d src) {
    double src_value;
    _mm_store_sd(&src_value, src);
    double result = std::log2(src_value);
    return _mm_load_sd(&result);
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_always();
    e.lea(e.r8, e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateLog2));
    e.vmovaps(i.dest, e.xmm0);
  }
};
struct LOG2_V128 : Sequence<LOG2_V128, I<OPCODE_LOG2, V128Op, V128Op>> {
  static __m128 EmulateLog2(void*, __m128 src) {
    alignas(16) float values[4];
    _mm_store_ps(values, src);
    for (size_t i = 0; i < 4; ++i) {
      values[i] = std::log2(values[i]);
    }
    return _mm_load_ps(values);
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.lea(e.r8, e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateLog2));
    e.vmovaps(i.dest, e.xmm0);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOG2, LOG2_F32, LOG2_F64, LOG2_V128);

// ============================================================================
// OPCODE_DOT_PRODUCT_3
// ============================================================================
struct DOT_PRODUCT_3_V128
    : Sequence<DOT_PRODUCT_3_V128,
               I<OPCODE_DOT_PRODUCT_3, F32Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // http://msdn.microsoft.com/en-us/library/bb514054(v=vs.90).aspx
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 // TODO(benvanik): apparently this is very slow
                                 // - find alternative?
                                 e.vdpps(dest, src1, src2, 0b01110001);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DOT_PRODUCT_3, DOT_PRODUCT_3_V128);

// ============================================================================
// OPCODE_DOT_PRODUCT_4
// ============================================================================
struct DOT_PRODUCT_4_V128
    : Sequence<DOT_PRODUCT_4_V128,
               I<OPCODE_DOT_PRODUCT_4, F32Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // http://msdn.microsoft.com/en-us/library/bb514054(v=vs.90).aspx
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 // TODO(benvanik): apparently this is very slow
                                 // - find alternative?
                                 e.vdpps(dest, src1, src2, 0b11110001);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DOT_PRODUCT_4, DOT_PRODUCT_4_V128);

// ============================================================================
// OPCODE_AND
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitAndXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitCommutativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const REG& src) {
        e.and_(dest_src, src);
      },
      [](X64Emitter& e, const REG& dest_src, int32_t constant) {
        e.and_(dest_src, constant);
      });
}
struct AND_I8 : Sequence<AND_I8, I<OPCODE_AND, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAndXX<AND_I8, Reg8>(e, i);
  }
};
struct AND_I16 : Sequence<AND_I16, I<OPCODE_AND, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAndXX<AND_I16, Reg16>(e, i);
  }
};
struct AND_I32 : Sequence<AND_I32, I<OPCODE_AND, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAndXX<AND_I32, Reg32>(e, i);
  }
};
struct AND_I64 : Sequence<AND_I64, I<OPCODE_AND, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitAndXX<AND_I64, Reg64>(e, i);
  }
};
struct AND_V128 : Sequence<AND_V128, I<OPCODE_AND, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vpand(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_AND, AND_I8, AND_I16, AND_I32, AND_I64, AND_V128);

// ============================================================================
// OPCODE_OR
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitOrXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitCommutativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const REG& src) {
        e.or_(dest_src, src);
      },
      [](X64Emitter& e, const REG& dest_src, int32_t constant) {
        e.or_(dest_src, constant);
      });
}
struct OR_I8 : Sequence<OR_I8, I<OPCODE_OR, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitOrXX<OR_I8, Reg8>(e, i);
  }
};
struct OR_I16 : Sequence<OR_I16, I<OPCODE_OR, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitOrXX<OR_I16, Reg16>(e, i);
  }
};
struct OR_I32 : Sequence<OR_I32, I<OPCODE_OR, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitOrXX<OR_I32, Reg32>(e, i);
  }
};
struct OR_I64 : Sequence<OR_I64, I<OPCODE_OR, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitOrXX<OR_I64, Reg64>(e, i);
  }
};
struct OR_V128 : Sequence<OR_V128, I<OPCODE_OR, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vpor(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_OR, OR_I8, OR_I16, OR_I32, OR_I64, OR_V128);

// ============================================================================
// OPCODE_XOR
// ============================================================================
// TODO(benvanik): put dest/src1|2 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitXorXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitCommutativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const REG& src) {
        e.xor_(dest_src, src);
      },
      [](X64Emitter& e, const REG& dest_src, int32_t constant) {
        e.xor_(dest_src, constant);
      });
}
struct XOR_I8 : Sequence<XOR_I8, I<OPCODE_XOR, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitXorXX<XOR_I8, Reg8>(e, i);
  }
};
struct XOR_I16 : Sequence<XOR_I16, I<OPCODE_XOR, I16Op, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitXorXX<XOR_I16, Reg16>(e, i);
  }
};
struct XOR_I32 : Sequence<XOR_I32, I<OPCODE_XOR, I32Op, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitXorXX<XOR_I32, Reg32>(e, i);
  }
};
struct XOR_I64 : Sequence<XOR_I64, I<OPCODE_XOR, I64Op, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitXorXX<XOR_I64, Reg64>(e, i);
  }
};
struct XOR_V128 : Sequence<XOR_V128, I<OPCODE_XOR, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(e, i,
                               [](X64Emitter& e, Xmm dest, Xmm src1, Xmm src2) {
                                 e.vpxor(dest, src1, src2);
                               });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_XOR, XOR_I8, XOR_I16, XOR_I32, XOR_I64, XOR_V128);

// ============================================================================
// OPCODE_NOT
// ============================================================================
// TODO(benvanik): put dest/src1 together.
template <typename SEQ, typename REG, typename ARGS>
void EmitNotXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitUnaryOp(
      e, i, [](X64Emitter& e, const REG& dest_src) { e.not_(dest_src); });
}
struct NOT_I8 : Sequence<NOT_I8, I<OPCODE_NOT, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNotXX<NOT_I8, Reg8>(e, i);
  }
};
struct NOT_I16 : Sequence<NOT_I16, I<OPCODE_NOT, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNotXX<NOT_I16, Reg16>(e, i);
  }
};
struct NOT_I32 : Sequence<NOT_I32, I<OPCODE_NOT, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNotXX<NOT_I32, Reg32>(e, i);
  }
};
struct NOT_I64 : Sequence<NOT_I64, I<OPCODE_NOT, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitNotXX<NOT_I64, Reg64>(e, i);
  }
};
struct NOT_V128 : Sequence<NOT_V128, I<OPCODE_NOT, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // dest = src ^ 0xFFFF...
    e.vpxor(i.dest, i.src1, e.GetXmmConstPtr(XMMFFFF /* FF... */));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_NOT, NOT_I8, NOT_I16, NOT_I32, NOT_I64, NOT_V128);

// ============================================================================
// OPCODE_SHL
// ============================================================================
// TODO(benvanik): optimize common shifts.
template <typename SEQ, typename REG, typename ARGS>
void EmitShlXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitAssociativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const Reg8& src) {
        // shlx: $1 = $2 << $3
        // shl: $1 = $1 << $2
        if (e.IsFeatureEnabled(kX64EmitBMI2)) {
          if (dest_src.getBit() == 64) {
            e.shlx(dest_src.cvt64(), dest_src.cvt64(), src.cvt64());
          } else {
            e.shlx(dest_src.cvt32(), dest_src.cvt32(), src.cvt32());
          }
        } else {
          e.mov(e.cl, src);
          e.shl(dest_src, e.cl);
        }
      },
      [](X64Emitter& e, const REG& dest_src, int8_t constant) {
        e.shl(dest_src, constant);
      });
}
struct SHL_I8 : Sequence<SHL_I8, I<OPCODE_SHL, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShlXX<SHL_I8, Reg8>(e, i);
  }
};
struct SHL_I16 : Sequence<SHL_I16, I<OPCODE_SHL, I16Op, I16Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShlXX<SHL_I16, Reg16>(e, i);
  }
};
struct SHL_I32 : Sequence<SHL_I32, I<OPCODE_SHL, I32Op, I32Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShlXX<SHL_I32, Reg32>(e, i);
  }
};
struct SHL_I64 : Sequence<SHL_I64, I<OPCODE_SHL, I64Op, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShlXX<SHL_I64, Reg64>(e, i);
  }
};
struct SHL_V128 : Sequence<SHL_V128, I<OPCODE_SHL, V128Op, V128Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): native version (with shift magic).
    if (i.src2.is_constant) {
      e.mov(e.r9, i.src2.constant());
    } else {
      e.mov(e.r9, i.src2);
    }
    e.lea(e.r8, e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateShlV128));
    e.vmovaps(i.dest, e.xmm0);
  }
  static __m128i EmulateShlV128(void*, __m128i src1, uint8_t src2) {
    // Almost all instances are shamt = 1, but non-constant.
    // shamt is [0,7]
    uint8_t shamt = src2 & 0x7;
    alignas(16) vec128_t value;
    _mm_store_si128(reinterpret_cast<__m128i*>(&value), src1);
    for (int i = 0; i < 15; ++i) {
      value.u8[i ^ 0x3] = (value.u8[i ^ 0x3] << shamt) |
                          (value.u8[(i + 1) ^ 0x3] >> (8 - shamt));
    }
    value.u8[15 ^ 0x3] = value.u8[15 ^ 0x3] << shamt;
    return _mm_load_si128(reinterpret_cast<__m128i*>(&value));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHL, SHL_I8, SHL_I16, SHL_I32, SHL_I64, SHL_V128);

// ============================================================================
// OPCODE_SHR
// ============================================================================
// TODO(benvanik): optimize common shifts.
template <typename SEQ, typename REG, typename ARGS>
void EmitShrXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitAssociativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const Reg8& src) {
        // shrx: op1 dest, op2 src, op3 count
        // shr: op1 src/dest, op2 count
        if (e.IsFeatureEnabled(kX64EmitBMI2)) {
          if (dest_src.getBit() == 64) {
            e.shrx(dest_src.cvt64(), dest_src.cvt64(), src.cvt64());
          } else if (dest_src.getBit() == 32) {
            e.shrx(dest_src.cvt32(), dest_src.cvt32(), src.cvt32());
          } else {
            e.movzx(dest_src.cvt32(), dest_src);
            e.shrx(dest_src.cvt32(), dest_src.cvt32(), src.cvt32());
          }
        } else {
          e.mov(e.cl, src);
          e.shr(dest_src, e.cl);
        }
      },
      [](X64Emitter& e, const REG& dest_src, int8_t constant) {
        e.shr(dest_src, constant);
      });
}
struct SHR_I8 : Sequence<SHR_I8, I<OPCODE_SHR, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShrXX<SHR_I8, Reg8>(e, i);
  }
};
struct SHR_I16 : Sequence<SHR_I16, I<OPCODE_SHR, I16Op, I16Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShrXX<SHR_I16, Reg16>(e, i);
  }
};
struct SHR_I32 : Sequence<SHR_I32, I<OPCODE_SHR, I32Op, I32Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShrXX<SHR_I32, Reg32>(e, i);
  }
};
struct SHR_I64 : Sequence<SHR_I64, I<OPCODE_SHR, I64Op, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitShrXX<SHR_I64, Reg64>(e, i);
  }
};
struct SHR_V128 : Sequence<SHR_V128, I<OPCODE_SHR, V128Op, V128Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): native version (with shift magic).
    if (i.src2.is_constant) {
      e.mov(e.r9, i.src2.constant());
    } else {
      e.mov(e.r9, i.src2);
    }
    e.lea(e.r8, e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateShrV128));
    e.vmovaps(i.dest, e.xmm0);
  }
  static __m128i EmulateShrV128(void*, __m128i src1, uint8_t src2) {
    // Almost all instances are shamt = 1, but non-constant.
    // shamt is [0,7]
    uint8_t shamt = src2 & 0x7;
    alignas(16) vec128_t value;
    _mm_store_si128(reinterpret_cast<__m128i*>(&value), src1);
    for (int i = 15; i > 0; --i) {
      value.u8[i ^ 0x3] = (value.u8[i ^ 0x3] >> shamt) |
                          (value.u8[(i - 1) ^ 0x3] << (8 - shamt));
    }
    value.u8[0 ^ 0x3] = value.u8[0 ^ 0x3] >> shamt;
    return _mm_load_si128(reinterpret_cast<__m128i*>(&value));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHR, SHR_I8, SHR_I16, SHR_I32, SHR_I64, SHR_V128);

// ============================================================================
// OPCODE_SHA
// ============================================================================
// TODO(benvanik): optimize common shifts.
template <typename SEQ, typename REG, typename ARGS>
void EmitSarXX(X64Emitter& e, const ARGS& i) {
  SEQ::EmitAssociativeBinaryOp(
      e, i,
      [](X64Emitter& e, const REG& dest_src, const Reg8& src) {
        if (e.IsFeatureEnabled(kX64EmitBMI2)) {
          if (dest_src.getBit() == 64) {
            e.sarx(dest_src.cvt64(), dest_src.cvt64(), src.cvt64());
          } else if (dest_src.getBit() == 32) {
            e.sarx(dest_src.cvt32(), dest_src.cvt32(), src.cvt32());
          } else {
            e.movsx(dest_src.cvt32(), dest_src);
            e.sarx(dest_src.cvt32(), dest_src.cvt32(), src.cvt32());
          }
        } else {
          e.mov(e.cl, src);
          e.sar(dest_src, e.cl);
        }
      },
      [](X64Emitter& e, const REG& dest_src, int8_t constant) {
        e.sar(dest_src, constant);
      });
}
struct SHA_I8 : Sequence<SHA_I8, I<OPCODE_SHA, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSarXX<SHA_I8, Reg8>(e, i);
  }
};
struct SHA_I16 : Sequence<SHA_I16, I<OPCODE_SHA, I16Op, I16Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSarXX<SHA_I16, Reg16>(e, i);
  }
};
struct SHA_I32 : Sequence<SHA_I32, I<OPCODE_SHA, I32Op, I32Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSarXX<SHA_I32, Reg32>(e, i);
  }
};
struct SHA_I64 : Sequence<SHA_I64, I<OPCODE_SHA, I64Op, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitSarXX<SHA_I64, Reg64>(e, i);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHA, SHA_I8, SHA_I16, SHA_I32, SHA_I64);

// ============================================================================
// OPCODE_VECTOR_SHL
// ============================================================================
struct VECTOR_SHL_V128
    : Sequence<VECTOR_SHL_V128, I<OPCODE_VECTOR_SHL, V128Op, V128Op, V128Op>> {
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
        assert_always();
        break;
    }
  }
  static __m128i EmulateVectorShlI8(void*, __m128i src1, __m128i src2) {
    alignas(16) uint8_t value[16];
    alignas(16) uint8_t shamt[16];
    _mm_store_si128(reinterpret_cast<__m128i*>(value), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(shamt), src2);
    for (size_t i = 0; i < 16; ++i) {
      value[i] = value[i] << (shamt[i] & 0x7);
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(value));
  }
  static void EmitInt8(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): native version (with shift magic).
    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      e.lea(e.r9, e.StashXmm(1, e.xmm0));
    } else {
      e.lea(e.r9, e.StashXmm(1, i.src2));
    }
    e.lea(e.r8, e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShlI8));
    e.vmovaps(i.dest, e.xmm0);
  }
  static __m128i EmulateVectorShlI16(void*, __m128i src1, __m128i src2) {
    alignas(16) uint16_t value[8];
    alignas(16) uint16_t shamt[8];
    _mm_store_si128(reinterpret_cast<__m128i*>(value), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(shamt), src2);
    for (size_t i = 0; i < 8; ++i) {
      value[i] = value[i] << (shamt[i] & 0xF);
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(value));
  }
  static void EmitInt16(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 8 - n; ++n) {
        if (shamt.u16[n] != shamt.u16[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same, so we can use vpsllw.
        e.vpsllw(i.dest, i.src1, shamt.u16[0] & 0xF);
        return;
      }
    }

    // Shift 8 words in src1 by amount specified in src2.
    Xbyak::Label emu, end;

    // Only bother with this check if shift amt isn't constant.
    if (!i.src2.is_constant) {
      // See if the shift is equal first for a shortcut.
      e.vpshuflw(e.xmm0, i.src2, 0b00000000);
      e.vpshufd(e.xmm0, e.xmm0, 0b00000000);
      e.vpxor(e.xmm1, e.xmm0, i.src2);
      e.vptest(e.xmm1, e.xmm1);
      e.jnz(emu);

      // Equal. Shift using vpsllw.
      e.mov(e.rax, 0xF);
      e.vmovq(e.xmm1, e.rax);
      e.vpand(e.xmm0, e.xmm0, e.xmm1);
      e.vpsllw(i.dest, i.src1, e.xmm0);
      e.jmp(end);
    }

    // TODO(benvanik): native version (with shift magic).
    e.L(emu);
    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      e.lea(e.r9, e.StashXmm(1, e.xmm0));
    } else {
      e.lea(e.r9, e.StashXmm(1, i.src2));
    }
    e.lea(e.r8, e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShlI16));
    e.vmovaps(i.dest, e.xmm0);

    e.L(end);
  }
  static __m128i EmulateVectorShlI32(void*, __m128i src1, __m128i src2) {
    alignas(16) uint32_t value[4];
    alignas(16) uint32_t shamt[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(value), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(shamt), src2);
    for (size_t i = 0; i < 4; ++i) {
      value[i] = value[i] << (shamt[i] & 0x1F);
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(value));
  }
  static void EmitInt32(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 4 - n; ++n) {
        if (shamt.u32[n] != shamt.u32[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same, so we can use vpslld.
        e.vpslld(i.dest, i.src1, shamt.u8[0] & 0x1F);
        return;
      }
    }

    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      if (i.src2.is_constant) {
        const auto& shamt = i.src2.constant();
        // Counts differ, so pre-mask and load constant.
        vec128_t masked = i.src2.constant();
        for (size_t n = 0; n < 4; ++n) {
          masked.u32[n] &= 0x1F;
        }
        e.LoadConstantXmm(e.xmm0, masked);
        e.vpsllvd(i.dest, i.src1, e.xmm0);
      } else {
        // Fully variable shift.
        // src shift mask may have values >31, and x86 sets to zero when
        // that happens so we mask.
        e.vandps(e.xmm0, i.src2, e.GetXmmConstPtr(XMMShiftMaskPS));
        e.vpsllvd(i.dest, i.src1, e.xmm0);
      }
    } else {
      // Shift 4 words in src1 by amount specified in src2.
      Xbyak::Label emu, end;

      // See if the shift is equal first for a shortcut.
      // Only bother with this check if shift amt isn't constant.
      if (!i.src2.is_constant) {
        e.vpshufd(e.xmm0, i.src2, 0b00000000);
        e.vpxor(e.xmm1, e.xmm0, i.src2);
        e.vptest(e.xmm1, e.xmm1);
        e.jnz(emu);

        // Equal. Shift using vpsrad.
        e.mov(e.rax, 0x1F);
        e.vmovq(e.xmm1, e.rax);
        e.vpand(e.xmm0, e.xmm0, e.xmm1);
        e.vpslld(i.dest, i.src1, e.xmm0);
        e.jmp(end);
      }

      // TODO(benvanik): native version (with shift magic).
      e.L(emu);
      if (i.src2.is_constant) {
        e.LoadConstantXmm(e.xmm0, i.src2.constant());
        e.lea(e.r9, e.StashXmm(1, e.xmm0));
      } else {
        e.lea(e.r9, e.StashXmm(1, i.src2));
      }
      e.lea(e.r8, e.StashXmm(0, i.src1));
      e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShlI32));
      e.vmovaps(i.dest, e.xmm0);

      e.L(end);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SHL, VECTOR_SHL_V128);

// ============================================================================
// OPCODE_VECTOR_SHR
// ============================================================================
struct VECTOR_SHR_V128
    : Sequence<VECTOR_SHR_V128, I<OPCODE_VECTOR_SHR, V128Op, V128Op, V128Op>> {
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
        assert_always();
        break;
    }
  }
  static __m128i EmulateVectorShrI8(void*, __m128i src1, __m128i src2) {
    alignas(16) uint8_t value[16];
    alignas(16) uint8_t shamt[16];
    _mm_store_si128(reinterpret_cast<__m128i*>(value), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(shamt), src2);
    for (size_t i = 0; i < 16; ++i) {
      value[i] = value[i] >> (shamt[i] & 0x7);
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(value));
  }
  static void EmitInt8(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): native version (with shift magic).
    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      e.lea(e.r9, e.StashXmm(1, e.xmm0));
    } else {
      e.lea(e.r9, e.StashXmm(1, i.src2));
    }
    e.lea(e.r8, e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShrI8));
    e.vmovaps(i.dest, e.xmm0);
  }
  static __m128i EmulateVectorShrI16(void*, __m128i src1, __m128i src2) {
    alignas(16) uint16_t value[8];
    alignas(16) uint16_t shamt[8];
    _mm_store_si128(reinterpret_cast<__m128i*>(value), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(shamt), src2);
    for (size_t i = 0; i < 8; ++i) {
      value[i] = value[i] >> (shamt[i] & 0xF);
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(value));
  }
  static void EmitInt16(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 8 - n; ++n) {
        if (shamt.u16[n] != shamt.u16[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same, so we can use vpsllw.
        e.vpsrlw(i.dest, i.src1, shamt.u16[0] & 0xF);
        return;
      }
    }

    // Shift 8 words in src1 by amount specified in src2.
    Xbyak::Label emu, end;

    // See if the shift is equal first for a shortcut.
    // Only bother with this check if shift amt isn't constant.
    if (!i.src2.is_constant) {
      e.vpshuflw(e.xmm0, i.src2, 0b00000000);
      e.vpshufd(e.xmm0, e.xmm0, 0b00000000);
      e.vpxor(e.xmm1, e.xmm0, i.src2);
      e.vptest(e.xmm1, e.xmm1);
      e.jnz(emu);

      // Equal. Shift using vpsrlw.
      e.mov(e.rax, 0xF);
      e.vmovq(e.xmm1, e.rax);
      e.vpand(e.xmm0, e.xmm0, e.xmm1);
      e.vpsrlw(i.dest, i.src1, e.xmm0);
      e.jmp(end);
    }

    // TODO(benvanik): native version (with shift magic).
    e.L(emu);
    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      e.lea(e.r9, e.StashXmm(1, e.xmm0));
    } else {
      e.lea(e.r9, e.StashXmm(1, i.src2));
    }
    e.lea(e.r8, e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShrI16));
    e.vmovaps(i.dest, e.xmm0);

    e.L(end);
  }
  static __m128i EmulateVectorShrI32(void*, __m128i src1, __m128i src2) {
    alignas(16) uint32_t value[4];
    alignas(16) uint32_t shamt[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(value), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(shamt), src2);
    for (size_t i = 0; i < 4; ++i) {
      value[i] = value[i] >> (shamt[i] & 0x1F);
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(value));
  }
  static void EmitInt32(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 4 - n; ++n) {
        if (shamt.u32[n] != shamt.u32[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same, so we can use vpsrld.
        e.vpsrld(i.dest, i.src1, shamt.u8[0] & 0x1F);
        return;
      } else {
        if (e.IsFeatureEnabled(kX64EmitAVX2)) {
          // Counts differ, so pre-mask and load constant.
          vec128_t masked = i.src2.constant();
          for (size_t n = 0; n < 4; ++n) {
            masked.u32[n] &= 0x1F;
          }
          e.LoadConstantXmm(e.xmm0, masked);
          e.vpsrlvd(i.dest, i.src1, e.xmm0);
          return;
        }
      }
    }

    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      // Fully variable shift.
      // src shift mask may have values >31, and x86 sets to zero when
      // that happens so we mask.
      e.vandps(e.xmm0, i.src2, e.GetXmmConstPtr(XMMShiftMaskPS));
      e.vpsrlvd(i.dest, i.src1, e.xmm0);
    } else {
      // Shift 4 words in src1 by amount specified in src2.
      Xbyak::Label emu, end;

      // See if the shift is equal first for a shortcut.
      // Only bother with this check if shift amt isn't constant.
      if (!i.src2.is_constant) {
        e.vpshufd(e.xmm0, i.src2, 0b00000000);
        e.vpxor(e.xmm1, e.xmm0, i.src2);
        e.vptest(e.xmm1, e.xmm1);
        e.jnz(emu);

        // Equal. Shift using vpsrld.
        e.mov(e.rax, 0x1F);
        e.vmovq(e.xmm1, e.rax);
        e.vpand(e.xmm0, e.xmm0, e.xmm1);
        e.vpsrld(i.dest, i.src1, e.xmm0);
        e.jmp(end);
      }

      // TODO(benvanik): native version.
      e.L(emu);
      if (i.src2.is_constant) {
        e.LoadConstantXmm(e.xmm0, i.src2.constant());
        e.lea(e.r9, e.StashXmm(1, e.xmm0));
      } else {
        e.lea(e.r9, e.StashXmm(1, i.src2));
      }
      e.lea(e.r8, e.StashXmm(0, i.src1));
      e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShrI32));
      e.vmovaps(i.dest, e.xmm0);

      e.L(end);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SHR, VECTOR_SHR_V128);

// ============================================================================
// OPCODE_VECTOR_SHA
// ============================================================================
struct VECTOR_SHA_V128
    : Sequence<VECTOR_SHA_V128, I<OPCODE_VECTOR_SHA, V128Op, V128Op, V128Op>> {
  static __m128i EmulateVectorShaI8(void*, __m128i src1, __m128i src2) {
    alignas(16) int8_t value[16];
    alignas(16) int8_t shamt[16];
    _mm_store_si128(reinterpret_cast<__m128i*>(value), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(shamt), src2);
    for (size_t i = 0; i < 16; ++i) {
      value[i] = value[i] >> (shamt[i] & 0x7);
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(value));
  }

  static void EmitInt8(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): native version (with shift magic).
    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      e.lea(e.r9, e.StashXmm(1, e.xmm0));
    } else {
      e.lea(e.r9, e.StashXmm(1, i.src2));
    }
    e.lea(e.r8, e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShaI8));
    e.vmovaps(i.dest, e.xmm0);
  }

  static __m128i EmulateVectorShaI16(void*, __m128i src1, __m128i src2) {
    alignas(16) int16_t value[8];
    alignas(16) int16_t shamt[8];
    _mm_store_si128(reinterpret_cast<__m128i*>(value), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(shamt), src2);
    for (size_t i = 0; i < 8; ++i) {
      value[i] = value[i] >> (shamt[i] & 0xF);
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(value));
  }

  static void EmitInt16(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 8 - n; ++n) {
        if (shamt.u16[n] != shamt.u16[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same, so we can use vpsraw.
        e.vpsraw(i.dest, i.src1, shamt.u16[0] & 0xF);
        return;
      }
    }

    // Shift 8 words in src1 by amount specified in src2.
    Xbyak::Label emu, end;

    // See if the shift is equal first for a shortcut.
    // Only bother with this check if shift amt isn't constant.
    if (!i.src2.is_constant) {
      e.vpshuflw(e.xmm0, i.src2, 0b00000000);
      e.vpshufd(e.xmm0, e.xmm0, 0b00000000);
      e.vpxor(e.xmm1, e.xmm0, i.src2);
      e.vptest(e.xmm1, e.xmm1);
      e.jnz(emu);

      // Equal. Shift using vpsraw.
      e.mov(e.rax, 0xF);
      e.vmovq(e.xmm1, e.rax);
      e.vpand(e.xmm0, e.xmm0, e.xmm1);
      e.vpsraw(i.dest, i.src1, e.xmm0);
      e.jmp(end);
    }

    // TODO(benvanik): native version (with shift magic).
    e.L(emu);
    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      e.lea(e.r9, e.StashXmm(1, e.xmm0));
    } else {
      e.lea(e.r9, e.StashXmm(1, i.src2));
    }
    e.lea(e.r8, e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShaI16));
    e.vmovaps(i.dest, e.xmm0);

    e.L(end);
  }

  static __m128i EmulateVectorShaI32(void*, __m128i src1, __m128i src2) {
    alignas(16) int32_t value[4];
    alignas(16) int32_t shamt[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(value), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(shamt), src2);
    for (size_t i = 0; i < 4; ++i) {
      value[i] = value[i] >> (shamt[i] & 0x1F);
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(value));
  }

  static void EmitInt32(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      const auto& shamt = i.src2.constant();
      bool all_same = true;
      for (size_t n = 0; n < 4 - n; ++n) {
        if (shamt.u32[n] != shamt.u32[n + 1]) {
          all_same = false;
          break;
        }
      }
      if (all_same) {
        // Every count is the same, so we can use vpsrad.
        e.vpsrad(i.dest, i.src1, shamt.u32[0] & 0x1F);
        return;
      }
    }

    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      // src shift mask may have values >31, and x86 sets to zero when
      // that happens so we mask.
      if (i.src2.is_constant) {
        e.LoadConstantXmm(e.xmm0, i.src2.constant());
        e.vandps(e.xmm0, e.GetXmmConstPtr(XMMShiftMaskPS));
      } else {
        e.vandps(e.xmm0, i.src2, e.GetXmmConstPtr(XMMShiftMaskPS));
      }
      e.vpsravd(i.dest, i.src1, e.xmm0);
    } else {
      // Shift 4 words in src1 by amount specified in src2.
      Xbyak::Label emu, end;

      // See if the shift is equal first for a shortcut.
      // Only bother with this check if shift amt isn't constant.
      if (!i.src2.is_constant) {
        e.vpshufd(e.xmm0, i.src2, 0b00000000);
        e.vpxor(e.xmm1, e.xmm0, i.src2);
        e.vptest(e.xmm1, e.xmm1);
        e.jnz(emu);

        // Equal. Shift using vpsrad.
        e.mov(e.rax, 0x1F);
        e.vmovq(e.xmm1, e.rax);
        e.vpand(e.xmm0, e.xmm0, e.xmm1);
        e.vpsrad(i.dest, i.src1, e.xmm0);
        e.jmp(end);
      }

      // TODO(benvanik): native version.
      e.L(emu);
      if (i.src2.is_constant) {
        e.LoadConstantXmm(e.xmm0, i.src2.constant());
        e.lea(e.r9, e.StashXmm(1, e.xmm0));
      } else {
        e.lea(e.r9, e.StashXmm(1, i.src2));
      }
      e.lea(e.r8, e.StashXmm(0, i.src1));
      e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorShaI32));
      e.vmovaps(i.dest, e.xmm0);

      e.L(end);
    }
  }

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
        assert_always();
        break;
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_SHA, VECTOR_SHA_V128);

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
  }
}
struct ROTATE_LEFT_I8
    : Sequence<ROTATE_LEFT_I8, I<OPCODE_ROTATE_LEFT, I8Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitRotateLeftXX<ROTATE_LEFT_I8, Reg8>(e, i);
  }
};
struct ROTATE_LEFT_I16
    : Sequence<ROTATE_LEFT_I16, I<OPCODE_ROTATE_LEFT, I16Op, I16Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitRotateLeftXX<ROTATE_LEFT_I16, Reg16>(e, i);
  }
};
struct ROTATE_LEFT_I32
    : Sequence<ROTATE_LEFT_I32, I<OPCODE_ROTATE_LEFT, I32Op, I32Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitRotateLeftXX<ROTATE_LEFT_I32, Reg32>(e, i);
  }
};
struct ROTATE_LEFT_I64
    : Sequence<ROTATE_LEFT_I64, I<OPCODE_ROTATE_LEFT, I64Op, I64Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitRotateLeftXX<ROTATE_LEFT_I64, Reg64>(e, i);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ROTATE_LEFT, ROTATE_LEFT_I8, ROTATE_LEFT_I16,
                     ROTATE_LEFT_I32, ROTATE_LEFT_I64);

// ============================================================================
// OPCODE_VECTOR_ROTATE_LEFT
// ============================================================================
// TODO(benvanik): AVX512 has a native variable rotate (rolv).
struct VECTOR_ROTATE_LEFT_V128
    : Sequence<VECTOR_ROTATE_LEFT_V128,
               I<OPCODE_VECTOR_ROTATE_LEFT, V128Op, V128Op, V128Op>> {
  static __m128i EmulateVectorRotateLeftI8(void*, __m128i src1, __m128i src2) {
    alignas(16) uint8_t value[16];
    alignas(16) uint8_t shamt[16];
    _mm_store_si128(reinterpret_cast<__m128i*>(value), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(shamt), src2);
    for (size_t i = 0; i < 16; ++i) {
      value[i] = xe::rotate_left<uint8_t>(value[i], shamt[i] & 0x7);
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(value));
  }
  static __m128i EmulateVectorRotateLeftI16(void*, __m128i src1, __m128i src2) {
    alignas(16) uint16_t value[8];
    alignas(16) uint16_t shamt[8];
    _mm_store_si128(reinterpret_cast<__m128i*>(value), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(shamt), src2);
    for (size_t i = 0; i < 8; ++i) {
      value[i] = xe::rotate_left<uint16_t>(value[i], shamt[i] & 0xF);
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(value));
  }
  static __m128i EmulateVectorRotateLeftI32(void*, __m128i src1, __m128i src2) {
    alignas(16) uint32_t value[4];
    alignas(16) uint32_t shamt[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(value), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(shamt), src2);
    for (size_t i = 0; i < 4; ++i) {
      value[i] = xe::rotate_left<uint32_t>(value[i], shamt[i] & 0x1F);
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(value));
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case INT8_TYPE:
        // TODO(benvanik): native version (with shift magic).
        e.lea(e.r8, e.StashXmm(0, i.src1));
        if (i.src2.is_constant) {
          e.LoadConstantXmm(e.xmm0, i.src2.constant());
          e.lea(e.r9, e.StashXmm(1, e.xmm0));
        } else {
          e.lea(e.r9, e.StashXmm(1, i.src2));
        }
        e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorRotateLeftI8));
        e.vmovaps(i.dest, e.xmm0);
        break;
      case INT16_TYPE:
        // TODO(benvanik): native version (with shift magic).
        e.lea(e.r8, e.StashXmm(0, i.src1));
        if (i.src2.is_constant) {
          e.LoadConstantXmm(e.xmm0, i.src2.constant());
          e.lea(e.r9, e.StashXmm(1, e.xmm0));
        } else {
          e.lea(e.r9, e.StashXmm(1, i.src2));
        }
        e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorRotateLeftI16));
        e.vmovaps(i.dest, e.xmm0);
        break;
      case INT32_TYPE: {
        if (e.IsFeatureEnabled(kX64EmitAVX2)) {
          Xmm temp = i.dest;
          if (i.dest == i.src1 || i.dest == i.src2) {
            temp = e.xmm2;
          }
          // Shift left (to get high bits):
          e.vpand(e.xmm0, i.src2, e.GetXmmConstPtr(XMMShiftMaskPS));
          e.vpsllvd(e.xmm1, i.src1, e.xmm0);
          // Shift right (to get low bits):
          e.vmovaps(temp, e.GetXmmConstPtr(XMMPI32));
          e.vpsubd(temp, e.xmm0);
          e.vpsrlvd(i.dest, i.src1, temp);
          // Merge:
          e.vpor(i.dest, e.xmm1);
        } else {
          // TODO(benvanik): non-AVX2 native version.
          e.lea(e.r8, e.StashXmm(0, i.src1));
          if (i.src2.is_constant) {
            e.LoadConstantXmm(e.xmm0, i.src2.constant());
            e.lea(e.r9, e.StashXmm(1, e.xmm0));
          } else {
            e.lea(e.r9, e.StashXmm(1, i.src2));
          }
          e.CallNativeSafe(reinterpret_cast<void*>(EmulateVectorRotateLeftI32));
          e.vmovaps(i.dest, e.xmm0);
        }
        break;
      }
      default:
        assert_always();
        break;
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_ROTATE_LEFT, VECTOR_ROTATE_LEFT_V128);

// ============================================================================
// OPCODE_VECTOR_AVERAGE
// ============================================================================
struct VECTOR_AVERAGE
    : Sequence<VECTOR_AVERAGE,
               I<OPCODE_VECTOR_AVERAGE, V128Op, V128Op, V128Op>> {
  static __m128i EmulateVectorAverageUnsignedI32(void*, __m128i src1,
                                                 __m128i src2) {
    alignas(16) uint32_t src1v[4];
    alignas(16) uint32_t src2v[4];
    alignas(16) uint32_t value[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(src1v), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(src2v), src2);
    for (size_t i = 0; i < 4; ++i) {
      auto t = (uint64_t(src1v[i]) + uint64_t(src2v[i]) + 1) >> 1;
      value[i] = uint32_t(t);
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(value));
  }
  static __m128i EmulateVectorAverageSignedI32(void*, __m128i src1,
                                               __m128i src2) {
    alignas(16) int32_t src1v[4];
    alignas(16) int32_t src2v[4];
    alignas(16) int32_t value[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(src1v), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(src2v), src2);
    for (size_t i = 0; i < 4; ++i) {
      auto t = (int64_t(src1v[i]) + int64_t(src2v[i]) + 1) >> 1;
      value[i] = int32_t(t);
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(value));
  }
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitCommutativeBinaryXmmOp(
        e, i,
        [&i](X64Emitter& e, const Xmm& dest, const Xmm& src1, const Xmm& src2) {
          const TypeName part_type =
              static_cast<TypeName>(i.instr->flags & 0xFF);
          const uint32_t arithmetic_flags = i.instr->flags >> 8;
          bool is_unsigned = !!(arithmetic_flags & ARITHMETIC_UNSIGNED);
          switch (part_type) {
            case INT8_TYPE:
              if (is_unsigned) {
                e.vpavgb(dest, src1, src2);
              } else {
                assert_always();
              }
              break;
            case INT16_TYPE:
              if (is_unsigned) {
                e.vpavgw(dest, src1, src2);
              } else {
                assert_always();
              }
              break;
            case INT32_TYPE:
              // No 32bit averages in AVX.
              if (is_unsigned) {
                if (i.src2.is_constant) {
                  e.LoadConstantXmm(e.xmm0, i.src2.constant());
                  e.lea(e.r9, e.StashXmm(1, e.xmm0));
                } else {
                  e.lea(e.r9, e.StashXmm(1, i.src2));
                }
                e.lea(e.r8, e.StashXmm(0, i.src1));
                e.CallNativeSafe(
                    reinterpret_cast<void*>(EmulateVectorAverageUnsignedI32));
                e.vmovaps(i.dest, e.xmm0);
              } else {
                if (i.src2.is_constant) {
                  e.LoadConstantXmm(e.xmm0, i.src2.constant());
                  e.lea(e.r9, e.StashXmm(1, e.xmm0));
                } else {
                  e.lea(e.r9, e.StashXmm(1, i.src2));
                }
                e.lea(e.r8, e.StashXmm(0, i.src1));
                e.CallNativeSafe(
                    reinterpret_cast<void*>(EmulateVectorAverageSignedI32));
                e.vmovaps(i.dest, e.xmm0);
              }
              break;
            default:
              assert_unhandled_case(part_type);
              break;
          }
        });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_VECTOR_AVERAGE, VECTOR_AVERAGE);

// ============================================================================
// OPCODE_BYTE_SWAP
// ============================================================================
// TODO(benvanik): put dest/src1 together.
struct BYTE_SWAP_I16
    : Sequence<BYTE_SWAP_I16, I<OPCODE_BYTE_SWAP, I16Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(
        e, i, [](X64Emitter& e, const Reg16& dest_src) { e.ror(dest_src, 8); });
  }
};
struct BYTE_SWAP_I32
    : Sequence<BYTE_SWAP_I32, I<OPCODE_BYTE_SWAP, I32Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(
        e, i, [](X64Emitter& e, const Reg32& dest_src) { e.bswap(dest_src); });
  }
};
struct BYTE_SWAP_I64
    : Sequence<BYTE_SWAP_I64, I<OPCODE_BYTE_SWAP, I64Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    EmitUnaryOp(
        e, i, [](X64Emitter& e, const Reg64& dest_src) { e.bswap(dest_src); });
  }
};
struct BYTE_SWAP_V128
    : Sequence<BYTE_SWAP_V128, I<OPCODE_BYTE_SWAP, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): find a way to do this without the memory load.
    e.vpshufb(i.dest, i.src1, e.GetXmmConstPtr(XMMByteSwapMask));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_BYTE_SWAP, BYTE_SWAP_I16, BYTE_SWAP_I32,
                     BYTE_SWAP_I64, BYTE_SWAP_V128);

// ============================================================================
// OPCODE_CNTLZ
// Count leading zeroes
// ============================================================================
struct CNTLZ_I8 : Sequence<CNTLZ_I8, I<OPCODE_CNTLZ, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (e.IsFeatureEnabled(kX64EmitLZCNT)) {
      // No 8bit lzcnt, so do 16 and sub 8.
      e.movzx(i.dest.reg().cvt16(), i.src1);
      e.lzcnt(i.dest.reg().cvt16(), i.dest.reg().cvt16());
      e.sub(i.dest, 8);
    } else {
      Xbyak::Label end;
      e.inLocalLabel();

      e.bsr(e.rax, i.src1);  // ZF set if i.src1 is 0
      e.mov(i.dest, 0x8);
      e.jz(end);

      e.xor_(e.rax, 0x7);
      e.mov(i.dest, e.rax);

      e.L(end);
      e.outLocalLabel();
    }
  }
};
struct CNTLZ_I16 : Sequence<CNTLZ_I16, I<OPCODE_CNTLZ, I8Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (e.IsFeatureEnabled(kX64EmitLZCNT)) {
      // LZCNT: searches $2 until MSB 1 found, stores idx (from last bit) in $1
      e.lzcnt(i.dest.reg().cvt32(), i.src1);
    } else {
      Xbyak::Label end;
      e.inLocalLabel();

      e.bsr(e.rax, i.src1);  // ZF set if i.src1 is 0
      e.mov(i.dest, 0x10);
      e.jz(end);

      e.xor_(e.rax, 0x0F);
      e.mov(i.dest, e.rax);

      e.L(end);
      e.outLocalLabel();
    }
  }
};
struct CNTLZ_I32 : Sequence<CNTLZ_I32, I<OPCODE_CNTLZ, I8Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (e.IsFeatureEnabled(kX64EmitLZCNT)) {
      e.lzcnt(i.dest.reg().cvt32(), i.src1);
    } else {
      Xbyak::Label end;
      e.inLocalLabel();

      e.bsr(e.rax, i.src1);  // ZF set if i.src1 is 0
      e.mov(i.dest, 0x20);
      e.jz(end);

      e.xor_(e.rax, 0x1F);
      e.mov(i.dest, e.rax);

      e.L(end);
      e.outLocalLabel();
    }
  }
};
struct CNTLZ_I64 : Sequence<CNTLZ_I64, I<OPCODE_CNTLZ, I8Op, I64Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (e.IsFeatureEnabled(kX64EmitLZCNT)) {
      e.lzcnt(i.dest.reg().cvt64(), i.src1);
    } else {
      Xbyak::Label end;
      e.inLocalLabel();

      e.bsr(e.rax, i.src1);  // ZF set if i.src1 is 0
      e.mov(i.dest, 0x40);
      e.jz(end);

      e.xor_(e.rax, 0x3F);
      e.mov(i.dest, e.rax);

      e.L(end);
      e.outLocalLabel();
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CNTLZ, CNTLZ_I8, CNTLZ_I16, CNTLZ_I32, CNTLZ_I64);

// ============================================================================
// OPCODE_INSERT
// ============================================================================
struct INSERT_I8
    : Sequence<INSERT_I8, I<OPCODE_INSERT, V128Op, V128Op, I8Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    e.vpinsrb(i.dest, i.src3.reg().cvt32(), i.src2.constant() ^ 0x3);
  }
};
struct INSERT_I16
    : Sequence<INSERT_I16, I<OPCODE_INSERT, V128Op, V128Op, I8Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    e.vpinsrw(i.dest, i.src3.reg().cvt32(), i.src2.constant() ^ 0x1);
  }
};
struct INSERT_I32
    : Sequence<INSERT_I32, I<OPCODE_INSERT, V128Op, V128Op, I8Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.is_constant);
    e.vpinsrd(i.dest, i.src3, i.src2.constant());
  }
};
EMITTER_OPCODE_TABLE(OPCODE_INSERT, INSERT_I8, INSERT_I16, INSERT_I32);

// ============================================================================
// OPCODE_EXTRACT
// ============================================================================
// TODO(benvanik): sequence extract/splat:
//  v0.i32 = extract v0.v128, 0
//  v0.v128 = splat v0.i32
// This can be a single broadcast.
struct EXTRACT_I8
    : Sequence<EXTRACT_I8, I<OPCODE_EXTRACT, I8Op, V128Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.vpextrb(i.dest.reg().cvt32(), i.src1, VEC128_B(i.src2.constant()));
    } else {
      e.mov(e.eax, 0x00000003);
      e.xor_(e.al, i.src2);
      e.and_(e.al, 0x1F);
      e.vmovd(e.xmm0, e.eax);
      e.vpshufb(e.xmm0, i.src1, e.xmm0);
      e.vmovd(i.dest.reg().cvt32(), e.xmm0);
      e.and_(i.dest, uint8_t(0xFF));
    }
  }
};
struct EXTRACT_I16
    : Sequence<EXTRACT_I16, I<OPCODE_EXTRACT, I16Op, V128Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      e.vpextrw(i.dest.reg().cvt32(), i.src1, VEC128_W(i.src2.constant()));
    } else {
      e.mov(e.al, i.src2);
      e.xor_(e.al, 0x01);
      e.shl(e.al, 1);
      e.mov(e.ah, e.al);
      e.add(e.ah, 1);
      e.vmovd(e.xmm0, e.eax);
      e.vpshufb(e.xmm0, i.src1, e.xmm0);
      e.vmovd(i.dest.reg().cvt32(), e.xmm0);
      e.and_(i.dest.reg().cvt32(), 0xFFFFu);
    }
  }
};
struct EXTRACT_I32
    : Sequence<EXTRACT_I32, I<OPCODE_EXTRACT, I32Op, V128Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    static const vec128_t extract_table_32[4] = {
        vec128b(3, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        vec128b(7, 6, 5, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        vec128b(11, 10, 9, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        vec128b(15, 14, 13, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    };
    if (i.src2.is_constant) {
      // TODO(gibbed): add support to constant propagation pass for
      // OPCODE_EXTRACT.
      Xmm src1;
      if (i.src1.is_constant) {
        src1 = e.xmm0;
        e.LoadConstantXmm(src1, i.src1.constant());
      } else {
        src1 = i.src1;
      }
      if (i.src2.constant() == 0) {
        e.vmovd(i.dest, src1);
      } else {
        e.vpextrd(i.dest, src1, VEC128_D(i.src2.constant()));
      }
    } else {
      // TODO(benvanik): try out hlide's version:
      // e.mov(e.eax, 3);
      // e.and_(e.al, i.src2);       // eax = [(i&3), 0, 0, 0]
      // e.imul(e.eax, 0x04040404); // [(i&3)*4, (i&3)*4, (i&3)*4, (i&3)*4]
      // e.add(e.eax, 0x00010203);  // [((i&3)*4)+3, ((i&3)*4)+2, ((i&3)*4)+1,
      // ((i&3)*4)+0]
      // e.vmovd(e.xmm0, e.eax);
      // e.vpshufb(e.xmm0, i.src1, e.xmm0);
      // e.vmovd(i.dest.reg().cvt32(), e.xmm0);
      // Get the desired word in xmm0, then extract that.
      Xmm src1;
      if (i.src1.is_constant) {
        src1 = e.xmm1;
        e.LoadConstantXmm(src1, i.src1.constant());
      } else {
        src1 = i.src1.reg();
      }

      e.xor_(e.rax, e.rax);
      e.mov(e.al, i.src2);
      e.and_(e.al, 0x03);
      e.shl(e.al, 4);
      e.mov(e.rdx, reinterpret_cast<uint64_t>(extract_table_32));
      e.vmovaps(e.xmm0, e.ptr[e.rdx + e.rax]);
      e.vpshufb(e.xmm0, src1, e.xmm0);
      e.vpextrd(i.dest, e.xmm0, 0);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_EXTRACT, EXTRACT_I8, EXTRACT_I16, EXTRACT_I32);

// ============================================================================
// OPCODE_SPLAT
// ============================================================================
// Copy a value into all elements of a vector
struct SPLAT_I8 : Sequence<SPLAT_I8, I<OPCODE_SPLAT, V128Op, I8Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      // TODO(benvanik): faster constant splats.
      e.mov(e.eax, i.src1.constant());
      e.vmovd(e.xmm0, e.eax);
    } else {
      e.vmovd(e.xmm0, i.src1.reg().cvt32());
    }

    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      e.vpbroadcastb(i.dest, e.xmm0);
    } else {
      e.vpunpcklbw(e.xmm0, e.xmm0);
      e.vpunpcklwd(e.xmm0, e.xmm0);
      e.vpshufd(i.dest, e.xmm0, 0);
    }
  }
};
struct SPLAT_I16 : Sequence<SPLAT_I16, I<OPCODE_SPLAT, V128Op, I16Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      // TODO(benvanik): faster constant splats.
      e.mov(e.eax, i.src1.constant());
      e.vmovd(e.xmm0, e.eax);
    } else {
      e.vmovd(e.xmm0, i.src1.reg().cvt32());
    }

    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      e.vpbroadcastw(i.dest, e.xmm0);
    } else {
      e.vpunpcklwd(e.xmm0, e.xmm0);  // unpack low word data
      e.vpshufd(i.dest, e.xmm0, 0);
    }
  }
};
struct SPLAT_I32 : Sequence<SPLAT_I32, I<OPCODE_SPLAT, V128Op, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      // TODO(benvanik): faster constant splats.
      e.mov(e.eax, i.src1.constant());
      e.vmovd(e.xmm0, e.eax);
    } else {
      e.vmovd(e.xmm0, i.src1);
    }

    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      e.vpbroadcastd(i.dest, e.xmm0);
    } else {
      e.vpshufd(i.dest, e.xmm0, 0);
    }
  }
};
struct SPLAT_F32 : Sequence<SPLAT_F32, I<OPCODE_SPLAT, V128Op, F32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    if (e.IsFeatureEnabled(kX64EmitAVX2)) {
      if (i.src1.is_constant) {
        // TODO(benvanik): faster constant splats.
        e.mov(e.eax, i.src1.value->constant.i32);
        e.vmovd(e.xmm0, e.eax);
        e.vbroadcastss(i.dest, e.xmm0);
      } else {
        e.vbroadcastss(i.dest, i.src1);
      }
    } else {
      if (i.src1.is_constant) {
        e.mov(e.eax, i.src1.value->constant.i32);
        e.vmovd(i.dest, e.eax);
        e.vshufps(i.dest, i.dest, i.dest, 0);
      } else {
        e.vshufps(i.dest, i.src1, i.src1, 0);
      }
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SPLAT, SPLAT_I8, SPLAT_I16, SPLAT_I32, SPLAT_F32);

// ============================================================================
// OPCODE_PERMUTE
// ============================================================================
struct PERMUTE_I32
    : Sequence<PERMUTE_I32, I<OPCODE_PERMUTE, V128Op, I32Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.instr->flags == INT32_TYPE);
    // Permute words between src2 and src3.
    // TODO(benvanik): check src3 for zero. if 0, we can use pshufb.
    if (i.src1.is_constant) {
      uint32_t control = i.src1.constant();
      // Shuffle things into the right places in dest & xmm0,
      // then we blend them together.
      uint32_t src_control =
          (((control >> 24) & 0x3) << 6) | (((control >> 16) & 0x3) << 4) |
          (((control >> 8) & 0x3) << 2) | (((control >> 0) & 0x3) << 0);

      uint32_t blend_control = 0;
      if (e.IsFeatureEnabled(kX64EmitAVX2)) {
        // Blender for vpblendd
        blend_control =
            (((control >> 26) & 0x1) << 3) | (((control >> 18) & 0x1) << 2) |
            (((control >> 10) & 0x1) << 1) | (((control >> 2) & 0x1) << 0);
      } else {
        // Blender for vpblendw
        blend_control =
            (((control >> 26) & 0x1) << 6) | (((control >> 18) & 0x1) << 4) |
            (((control >> 10) & 0x1) << 2) | (((control >> 2) & 0x1) << 0);
        blend_control |= blend_control << 1;
      }

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
      } else {
        e.vmovaps(e.xmm0, src3);
        e.vpshufd(i.dest, src2, src_control);
        e.vpshufd(e.xmm0, e.xmm0, src_control);
      }

      if (e.IsFeatureEnabled(kX64EmitAVX2)) {
        e.vpblendd(i.dest, e.xmm0, blend_control);  // $0 = $1 <blend> $2
      } else {
        e.vpblendw(i.dest, e.xmm0, blend_control);  // $0 = $1 <blend> $2
      }
    } else {
      // Permute by non-constant.
      assert_always();
    }
  }
};
struct PERMUTE_V128
    : Sequence<PERMUTE_V128,
               I<OPCODE_PERMUTE, V128Op, V128Op, V128Op, V128Op>> {
  static void EmitByInt8(X64Emitter& e, const EmitArgType& i) {
    // TODO(benvanik): find out how to do this with only one temp register!
    // Permute bytes between src2 and src3.
    // src1 is an array of indices corresponding to positions within src2 and
    // src3.
    if (i.src3.value->IsConstantZero()) {
      // Permuting with src2/zero, so just shuffle/mask.
      if (i.src2.value->IsConstantZero()) {
        // src2 & src3 are zero, so result will always be zero.
        e.vpxor(i.dest, i.dest);
      } else {
        // Control mask needs to be shuffled.
        if (i.src1.is_constant) {
          e.LoadConstantXmm(e.xmm0, i.src1.constant());
          e.vxorps(e.xmm0, e.xmm0, e.GetXmmConstPtr(XMMSwapWordMask));
        } else {
          e.vxorps(e.xmm0, i.src1, e.GetXmmConstPtr(XMMSwapWordMask));
        }
        e.vpand(e.xmm0, e.GetXmmConstPtr(XMMPermuteByteMask));
        if (i.src2.is_constant) {
          e.LoadConstantXmm(i.dest, i.src2.constant());
          e.vpshufb(i.dest, i.dest, e.xmm0);
        } else {
          e.vpshufb(i.dest, i.src2, e.xmm0);
        }
        // Build a mask with values in src2 having 0 and values in src3 having
        // 1.
        e.vpcmpgtb(e.xmm0, e.xmm0, e.GetXmmConstPtr(XMMPermuteControl15));
        e.vpandn(i.dest, e.xmm0, i.dest);
      }
    } else {
      // General permute.
      // Control mask needs to be shuffled.
      // TODO(benvanik): do constants here instead of in generated code.
      if (i.src1.is_constant) {
        e.LoadConstantXmm(e.xmm2, i.src1.constant());
        e.vxorps(e.xmm2, e.xmm2, e.GetXmmConstPtr(XMMSwapWordMask));
      } else {
        e.vxorps(e.xmm2, i.src1, e.GetXmmConstPtr(XMMSwapWordMask));
      }
      e.vpand(e.xmm2, e.GetXmmConstPtr(XMMPermuteByteMask));
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

  static void EmitByInt16(X64Emitter& e, const EmitArgType& i) {
    // src1 is an array of indices corresponding to positions within src2 and
    // src3.
    assert_true(i.src1.is_constant);
    vec128_t perm = (i.src1.constant() & vec128s(0xF)) ^ vec128s(0x1);
    vec128_t perm_ctrl = vec128b(0);
    for (int i = 0; i < 8; i++) {
      perm_ctrl.i16[i] = perm.i16[i] > 7 ? -1 : 0;

      auto v = uint8_t(perm.u16[i]);
      perm.u8[i * 2] = v * 2;
      perm.u8[i * 2 + 1] = v * 2 + 1;
    }
    e.LoadConstantXmm(e.xmm0, perm);

    if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm1, i.src2.constant());
    } else {
      e.vmovdqa(e.xmm1, i.src2);
    }
    if (i.src3.is_constant) {
      e.LoadConstantXmm(e.xmm2, i.src3.constant());
    } else {
      e.vmovdqa(e.xmm2, i.src3);
    }

    e.vpshufb(e.xmm1, e.xmm1, e.xmm0);
    e.vpshufb(e.xmm2, e.xmm2, e.xmm0);

    uint8_t mask = 0;
    for (int i = 0; i < 8; i++) {
      if (perm_ctrl.i16[i] == 0) {
        mask |= 1 << (7 - i);
      }
    }
    e.vpblendw(i.dest, e.xmm1, e.xmm2, mask);
  }

  static void EmitByInt32(X64Emitter& e, const EmitArgType& i) {
    assert_always();
  }

  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags) {
      case INT8_TYPE:
        EmitByInt8(e, i);
        break;
      case INT16_TYPE:
        EmitByInt16(e, i);
        break;
      case INT32_TYPE:
        EmitByInt32(e, i);
        break;
      default:
        assert_unhandled_case(i.instr->flags);
        return;
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_PERMUTE, PERMUTE_I32, PERMUTE_V128);

// ============================================================================
// OPCODE_SWIZZLE
// ============================================================================
struct SWIZZLE
    : Sequence<SWIZZLE, I<OPCODE_SWIZZLE, V128Op, V128Op, OffsetOp>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    auto element_type = i.instr->flags;
    if (element_type == INT8_TYPE) {
      assert_always();
    } else if (element_type == INT16_TYPE) {
      assert_always();
    } else if (element_type == INT32_TYPE || element_type == FLOAT32_TYPE) {
      uint8_t swizzle_mask = static_cast<uint8_t>(i.src2.value);
      Xmm src1;
      if (i.src1.is_constant) {
        src1 = e.xmm0;
        e.LoadConstantXmm(src1, i.src1.constant());
      } else {
        src1 = i.src1;
      }
      e.vpshufd(i.dest, src1, swizzle_mask);
    } else if (element_type == INT64_TYPE || element_type == FLOAT64_TYPE) {
      assert_always();
    } else {
      assert_always();
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SWIZZLE, SWIZZLE);

// ============================================================================
// OPCODE_PACK
// ============================================================================
struct PACK : Sequence<PACK, I<OPCODE_PACK, V128Op, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags & PACK_TYPE_MODE) {
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
      case PACK_TYPE_SHORT_4:
        EmitSHORT_4(e, i);
        break;
      case PACK_TYPE_UINT_2101010:
        EmitUINT_2101010(e, i);
        break;
      case PACK_TYPE_8_IN_16:
        Emit8_IN_16(e, i, i.instr->flags);
        break;
      case PACK_TYPE_16_IN_32:
        Emit16_IN_32(e, i, i.instr->flags);
        break;
      default:
        assert_unhandled_case(i.instr->flags);
        break;
    }
  }
  static void EmitD3DCOLOR(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->IsConstantZero());
    // Saturate to [3,3....] so that only values between 3...[00] and 3...[FF]
    // are valid.
    if (i.src1.is_constant) {
      e.LoadConstantXmm(i.dest, i.src1.constant());
      e.vminps(i.dest, i.dest, e.GetXmmConstPtr(XMMPackD3DCOLORSat));
    } else {
      e.vminps(i.dest, i.src1, e.GetXmmConstPtr(XMMPackD3DCOLORSat));
    }
    e.vmaxps(i.dest, i.dest, e.GetXmmConstPtr(XMM3333));
    // Extract bytes.
    // RGBA (XYZW) -> ARGB (WXYZ)
    // w = ((src1.uw & 0xFF) << 24) | ((src1.ux & 0xFF) << 16) |
    //     ((src1.uy & 0xFF) << 8) | (src1.uz & 0xFF)
    e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMPackD3DCOLOR));
  }
  static __m128i EmulateFLOAT16_2(void*, __m128 src1) {
    alignas(16) float a[4];
    alignas(16) uint16_t b[8];
    _mm_store_ps(a, src1);
    std::memset(b, 0, sizeof(b));

    for (int i = 0; i < 2; i++) {
      b[7 - i] = half_float::detail::float2half<std::round_toward_zero>(a[i]);
    }

    return _mm_load_si128(reinterpret_cast<__m128i*>(b));
  }
  static void EmitFLOAT16_2(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->IsConstantZero());
    // http://blogs.msdn.com/b/chuckw/archive/2012/09/11/directxmath-f16c-and-fma.aspx
    // dest = [(src1.x | src1.y), 0, 0, 0]

    if (e.IsFeatureEnabled(kX64EmitF16C)) {
      // 0|0|0|0|W|Z|Y|X
      e.vcvtps2ph(i.dest, i.dest, 0b00000011);
      // Shuffle to X|Y|0|0|0|0|0|0
      e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMPackFLOAT16_2));
    } else {
      e.lea(e.r8, e.StashXmm(0, i.src1));
      e.CallNativeSafe(reinterpret_cast<void*>(EmulateFLOAT16_2));
      e.vmovaps(i.dest, e.xmm0);
    }
  }
  static __m128i EmulateFLOAT16_4(void*, __m128 src1) {
    alignas(16) float a[4];
    alignas(16) uint16_t b[8];
    _mm_store_ps(a, src1);
    std::memset(b, 0, sizeof(b));

    for (int i = 0; i < 4; i++) {
      b[7 - i] = half_float::detail::float2half<std::round_toward_zero>(a[i]);
    }

    return _mm_load_si128(reinterpret_cast<__m128i*>(b));
  }
  static void EmitFLOAT16_4(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->IsConstantZero());
    // dest = [(src1.x | src1.y), (src1.z | src1.w), 0, 0]

    if (e.IsFeatureEnabled(kX64EmitF16C)) {
      // 0|0|0|0|W|Z|Y|X
      e.vcvtps2ph(i.dest, i.src1, 0b00000011);
      // Shuffle to X|Y|Z|W|0|0|0|0
      e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMPackFLOAT16_4));
    } else {
      e.lea(e.r8, e.StashXmm(0, i.src1));
      e.CallNativeSafe(reinterpret_cast<void*>(EmulateFLOAT16_4));
      e.vmovaps(i.dest, e.xmm0);
    }
  }
  static void EmitSHORT_2(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->IsConstantZero());
    // Saturate.
    e.vmaxps(i.dest, i.src1, e.GetXmmConstPtr(XMMPackSHORT_Min));
    e.vminps(i.dest, i.dest, e.GetXmmConstPtr(XMMPackSHORT_Max));
    // Pack.
    e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMPackSHORT_2));
  }
  static void EmitSHORT_4(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->IsConstantZero());
    // Saturate.
    e.vmaxps(i.dest, i.src1, e.GetXmmConstPtr(XMMPackSHORT_Min));
    e.vminps(i.dest, i.dest, e.GetXmmConstPtr(XMMPackSHORT_Max));
    // Pack.
    e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMPackSHORT_4));
  }
  static __m128i EmulatePackUINT_2101010(void*, __m128i src1) {
    // https://www.opengl.org/registry/specs/ARB/vertex_type_2_10_10_10_rev.txt
    union {
      alignas(16) int32_t a_i[4];
      alignas(16) uint32_t a_u[4];
      alignas(16) float a_f[4];
    };
    alignas(16) uint32_t b[4];
    alignas(16) uint32_t c[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(a_u), src1);
    // XYZ are 10 bits, signed and saturated.
    for (int i = 0; i < 3; ++i) {
      static const int32_t kMinValueXYZ = 0x403FFE01;  // 3 - 1FF / (1 << 22)
      static const int32_t kMaxValueXYZ = 0x404001FF;  // 3 + 1FF / (1 << 22)
      uint32_t exponent = (a_u[i] >> 23) & 0xFF;
      uint32_t fractional = a_u[i] & 0x007FFFFF;
      if ((exponent == 0xFF) && fractional) {
        b[i] = 0x200;
      } else if (a_i[i] > kMaxValueXYZ) {
        b[i] = 0x1FF;  // INT_MAX
      } else if (a_i[i] < kMinValueXYZ) {
        b[i] = 0x201;  // -INT_MAX
      } else {
        b[i] = a_u[i] & 0x3FF;
      }
    }
    // W is 2 bits, unsigned and saturated.
    static const int32_t kMinValueW = 0x40400000;  // 3
    static const int32_t kMaxValueW = 0x40400003;  // 3 + 3 / (1 << 22)
    uint32_t w_exponent = (a_u[3] >> 23) & 0xFF;
    uint32_t w_fractional = a_u[3] & 0x007FFFFF;
    if ((w_exponent == 0xFF) && w_fractional) {
      b[3] = 0x0;
    } else if (a_i[3] > kMaxValueW) {
      b[3] = 0x3;
    } else if (a_i[3] < kMinValueW) {
      b[3] = 0x0;
    } else {
      b[3] = a_u[3] & 0x3;
    }
    // Combine in 2101010 WZYX.
    c[0] = c[1] = c[2] = 0;
    c[3] = ((b[3] & 0x3) << 30) | ((b[2] & 0x3FF) << 20) |
           ((b[1] & 0x3FF) << 10) | ((b[0] & 0x3FF));
    return _mm_load_si128(reinterpret_cast<__m128i*>(c));
  }
  static void EmitUINT_2101010(X64Emitter& e, const EmitArgType& i) {
    assert_true(i.src2.value->IsConstantZero());
    // dest = [(b2(src1.w), b10(src1.z), b10(src1.y), b10(src1.x)), 0, 0, 0]
    // TODO(benvanik): optimized version.
    e.lea(e.r8, e.StashXmm(0, i.src1));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulatePackUINT_2101010));
    e.vmovaps(i.dest, e.xmm0);
  }
  static __m128i EmulatePack8_IN_16_UN_UN_SAT(void*, __m128i src1,
                                              __m128i src2) {
    alignas(16) uint16_t a[8];
    alignas(16) uint16_t b[8];
    alignas(16) uint8_t c[16];
    _mm_store_si128(reinterpret_cast<__m128i*>(a), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(b), src2);
    for (int i = 0; i < 8; ++i) {
      c[i] = uint8_t(std::max(uint16_t(0), std::min(uint16_t(255), a[i])));
      c[i + 8] = uint8_t(std::max(uint16_t(0), std::min(uint16_t(255), b[i])));
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(c));
  }
  static __m128i EmulatePack8_IN_16_UN_UN(void*, __m128i src1, __m128i src2) {
    alignas(16) uint8_t a[16];
    alignas(16) uint8_t b[16];
    alignas(16) uint8_t c[16];
    _mm_store_si128(reinterpret_cast<__m128i*>(a), src1);
    _mm_store_si128(reinterpret_cast<__m128i*>(b), src2);
    for (int i = 0; i < 8; ++i) {
      c[i] = a[i * 2];
      c[i + 8] = b[i * 2];
    }
    return _mm_load_si128(reinterpret_cast<__m128i*>(c));
  }
  static void Emit8_IN_16(X64Emitter& e, const EmitArgType& i, uint32_t flags) {
    // TODO(benvanik): handle src2 (or src1) being constant zero
    if (IsPackInUnsigned(flags)) {
      if (IsPackOutUnsigned(flags)) {
        if (IsPackOutSaturate(flags)) {
          // unsigned -> unsigned + saturate
          if (i.src2.is_constant) {
            e.LoadConstantXmm(e.xmm0, i.src2.constant());
            e.lea(e.r9, e.StashXmm(1, e.xmm0));
          } else {
            e.lea(e.r9, e.StashXmm(1, i.src2));
          }
          e.lea(e.r8, e.StashXmm(0, i.src1));
          e.CallNativeSafe(
              reinterpret_cast<void*>(EmulatePack8_IN_16_UN_UN_SAT));
          e.vmovaps(i.dest, e.xmm0);
          e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMByteOrderMask));
        } else {
          // unsigned -> unsigned
          e.lea(e.r9, e.StashXmm(1, i.src2));
          e.lea(e.r8, e.StashXmm(0, i.src1));
          e.CallNativeSafe(reinterpret_cast<void*>(EmulatePack8_IN_16_UN_UN));
          e.vmovaps(i.dest, e.xmm0);
          e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMByteOrderMask));
        }
      } else {
        if (IsPackOutSaturate(flags)) {
          // unsigned -> signed + saturate
          assert_always();
        } else {
          // unsigned -> signed
          assert_always();
        }
      }
    } else {
      if (IsPackOutUnsigned(flags)) {
        if (IsPackOutSaturate(flags)) {
          // signed -> unsigned + saturate
          // PACKUSWB / SaturateSignedWordToUnsignedByte
          Xbyak::Xmm src2 = i.src2.is_constant ? e.xmm0 : i.src2;
          if (i.src2.is_constant) {
            e.LoadConstantXmm(src2, i.src2.constant());
          }

          e.vpackuswb(i.dest, i.src1, src2);
          e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMByteOrderMask));
        } else {
          // signed -> unsigned
          assert_always();
        }
      } else {
        if (IsPackOutSaturate(flags)) {
          // signed -> signed + saturate
          // PACKSSWB / SaturateSignedWordToSignedByte
          e.vpacksswb(i.dest, i.src1, i.src2);
          e.vpshufb(i.dest, i.dest, e.GetXmmConstPtr(XMMByteOrderMask));
        } else {
          // signed -> signed
          assert_always();
        }
      }
    }
  }
  // Pack 2 32-bit vectors into a 16-bit vector.
  static void Emit16_IN_32(X64Emitter& e, const EmitArgType& i,
                           uint32_t flags) {
    // TODO(benvanik): handle src2 (or src1) being constant zero
    if (IsPackInUnsigned(flags)) {
      if (IsPackOutUnsigned(flags)) {
        if (IsPackOutSaturate(flags)) {
          // unsigned -> unsigned + saturate
          // Construct a saturation max value
          e.mov(e.eax, 0xFFFFu);
          e.vmovd(e.xmm0, e.eax);
          e.vpshufd(e.xmm0, e.xmm0, 0b00000000);

          if (!i.src1.is_constant) {
            e.vpminud(e.xmm1, i.src1, e.xmm0);  // Saturate src1
            e.vpshuflw(e.xmm1, e.xmm1, 0b00100010);
            e.vpshufhw(e.xmm1, e.xmm1, 0b00100010);
            e.vpshufd(e.xmm1, e.xmm1, 0b00001000);
          } else {
            // TODO(DrChat): Non-zero constants
            assert_true(i.src1.constant().u64[0] == 0 &&
                        i.src1.constant().u64[1] == 0);
            e.vpxor(e.xmm1, e.xmm1);
          }

          if (!i.src2.is_constant) {
            e.vpminud(i.dest, i.src2, e.xmm0);  // Saturate src2
            e.vpshuflw(i.dest, i.dest, 0b00100010);
            e.vpshufhw(i.dest, i.dest, 0b00100010);
            e.vpshufd(i.dest, i.dest, 0b10000000);
          } else {
            // TODO(DrChat): Non-zero constants
            assert_true(i.src2.constant().u64[0] == 0 &&
                        i.src2.constant().u64[1] == 0);
            e.vpxor(i.dest, i.dest);
          }

          e.vpblendw(i.dest, i.dest, e.xmm1, 0b00001111);
        } else {
          // unsigned -> unsigned
          e.vmovaps(e.xmm0, i.src1);
          e.vpshuflw(e.xmm0, e.xmm0, 0b00100010);
          e.vpshufhw(e.xmm0, e.xmm0, 0b00100010);
          e.vpshufd(e.xmm0, e.xmm0, 0b00001000);

          e.vmovaps(i.dest, i.src2);
          e.vpshuflw(i.dest, i.dest, 0b00100010);
          e.vpshufhw(i.dest, i.dest, 0b00100010);
          e.vpshufd(i.dest, i.dest, 0b10000000);

          e.vpblendw(i.dest, i.dest, e.xmm0, 0b00001111);
        }
      } else {
        if (IsPackOutSaturate(flags)) {
          // unsigned -> signed + saturate
          assert_always();
        } else {
          // unsigned -> signed
          assert_always();
        }
      }
    } else {
      if (IsPackOutUnsigned(flags)) {
        if (IsPackOutSaturate(flags)) {
          // signed -> unsigned + saturate
          // PACKUSDW
          // TMP[15:0] <- (DEST[31:0] < 0) ? 0 : DEST[15:0];
          // DEST[15:0] <- (DEST[31:0] > FFFFH) ? FFFFH : TMP[15:0];
          e.vpackusdw(i.dest, i.src1, i.src2);
          e.vpshuflw(i.dest, i.dest, 0b10110001);
          e.vpshufhw(i.dest, i.dest, 0b10110001);
        } else {
          // signed -> unsigned
          assert_always();
        }
      } else {
        if (IsPackOutSaturate(flags)) {
          // signed -> signed + saturate
          // PACKSSDW / SaturateSignedDwordToSignedWord
          Xmm src2;
          if (!i.src2.is_constant) {
            src2 = i.src2;
          } else {
            assert_false(i.src1 == e.xmm0);
            src2 = e.xmm0;
            e.LoadConstantXmm(src2, i.src2.constant());
          }
          e.vpackssdw(i.dest, i.src1, src2);
          e.vpshuflw(i.dest, i.dest, 0b10110001);
          e.vpshufhw(i.dest, i.dest, 0b10110001);
        } else {
          // signed -> signed
          assert_always();
        }
      }
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_PACK, PACK);

// ============================================================================
// OPCODE_UNPACK
// ============================================================================
struct UNPACK : Sequence<UNPACK, I<OPCODE_UNPACK, V128Op, V128Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    switch (i.instr->flags & PACK_TYPE_MODE) {
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
      case PACK_TYPE_SHORT_4:
        EmitSHORT_4(e, i);
        break;
      case PACK_TYPE_UINT_2101010:
        EmitUINT_2101010(e, i);
        break;
      case PACK_TYPE_8_IN_16:
        Emit8_IN_16(e, i, i.instr->flags);
        break;
      case PACK_TYPE_16_IN_32:
        Emit16_IN_32(e, i, i.instr->flags);
        break;
      default:
        assert_unhandled_case(i.instr->flags);
        break;
    }
  }
  static void EmitD3DCOLOR(X64Emitter& e, const EmitArgType& i) {
    // ARGB (WXYZ) -> RGBA (XYZW)
    // XMLoadColor
    if (i.src1.is_constant) {
      if (i.src1.value->IsConstantZero()) {
        e.vmovaps(i.dest, e.GetXmmConstPtr(XMMOne));
        return;
      } else {
        assert_always();
      }
    }
    // src = ZZYYXXWW
    // Unpack to 000000ZZ,000000YY,000000XX,000000WW
    e.vpshufb(i.dest, i.src1, e.GetXmmConstPtr(XMMUnpackD3DCOLOR));
    // Add 1.0f to each.
    e.vpor(i.dest, e.GetXmmConstPtr(XMMOne));
  }
  static __m128 EmulateFLOAT16_2(void*, __m128i src1) {
    alignas(16) uint16_t a[8];
    alignas(16) float b[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(a), src1);

    for (int i = 0; i < 2; i++) {
      b[i] = half_float::detail::half2float(a[VEC128_W(6 + i)]);
    }

    // Constants, or something
    b[2] = 0.f;
    b[3] = 1.f;

    return _mm_load_ps(b);
  }
  static void EmitFLOAT16_2(X64Emitter& e, const EmitArgType& i) {
    // 1 bit sign, 5 bit exponent, 10 bit mantissa
    // D3D10 half float format
    // TODO(benvanik):
    // http://blogs.msdn.com/b/chuckw/archive/2012/09/11/directxmath-f16c-and-fma.aspx
    // Use _mm_cvtph_ps -- requires very modern processors (SSE5+)
    // Unpacking half floats:
    // http://fgiesen.wordpress.com/2012/03/28/half-to-float-done-quic/
    // Packing half floats: https://gist.github.com/rygorous/2156668
    // Load source, move from tight pack of X16Y16.... to X16...Y16...
    // Also zero out the high end.
    // TODO(benvanik): special case constant unpacks that just get 0/1/etc.

    if (e.IsFeatureEnabled(kX64EmitF16C)) {
      // sx = src.iw >> 16;
      // sy = src.iw & 0xFFFF;
      // dest = { XMConvertHalfToFloat(sx),
      //          XMConvertHalfToFloat(sy),
      //          0.0,
      //          1.0 };
      // Shuffle to 0|0|0|0|0|0|Y|X
      e.vpshufb(i.dest, i.src1, e.GetXmmConstPtr(XMMUnpackFLOAT16_2));
      e.vcvtph2ps(i.dest, i.dest);
      e.vpshufd(i.dest, i.dest, 0b10100100);
      e.vpor(i.dest, e.GetXmmConstPtr(XMM0001));
    } else {
      Xmm src;
      if (i.src1.is_constant) {
        src = e.xmm0;
        e.LoadConstantXmm(src, i.src1.constant());
      } else {
        src = i.src1;
      }

      e.lea(e.r8, e.StashXmm(0, src));
      e.CallNativeSafe(reinterpret_cast<void*>(EmulateFLOAT16_2));
      e.vmovaps(i.dest, e.xmm0);
    }
  }
  static __m128 EmulateFLOAT16_4(void*, __m128i src1) {
    alignas(16) uint16_t a[8];
    alignas(16) float b[4];
    _mm_store_si128(reinterpret_cast<__m128i*>(a), src1);

    for (int i = 0; i < 4; i++) {
      b[i] = half_float::detail::half2float(a[VEC128_W(4 + i)]);
    }

    return _mm_load_ps(b);
  }
  static void EmitFLOAT16_4(X64Emitter& e, const EmitArgType& i) {
    // src = [(dest.x | dest.y), (dest.z | dest.w), 0, 0]

    if (e.IsFeatureEnabled(kX64EmitF16C)) {
      // Shuffle to 0|0|0|0|W|Z|Y|X
      e.vpshufb(i.dest, i.src1, e.GetXmmConstPtr(XMMUnpackFLOAT16_4));
      e.vcvtph2ps(i.dest, i.dest);
    } else {
      e.lea(e.r8, e.StashXmm(0, i.src1));
      e.CallNativeSafe(reinterpret_cast<void*>(EmulateFLOAT16_4));
      e.vmovaps(i.dest, e.xmm0);
    }
  }
  static void EmitSHORT_2(X64Emitter& e, const EmitArgType& i) {
    // (VD.x) = 3.0 + (VB.x>>16)*2^-22
    // (VD.y) = 3.0 + (VB.x)*2^-22
    // (VD.z) = 0.0
    // (VD.w) = 1.0

    // XMLoadShortN2 plus 3,3,0,3 (for some reason)
    // src is (xx,xx,xx,VALUE)
    // (VALUE,VALUE,VALUE,VALUE)
    Xmm src;
    if (i.src1.is_constant) {
      if (i.src1.value->IsConstantZero()) {
        e.vmovdqa(i.dest, e.GetXmmConstPtr(XMM3301));
        return;
      } else {
        // TODO(benvanik): check other common constants/perform shuffle/or here.
        src = e.xmm0;
        e.LoadConstantXmm(src, i.src1.constant());
      }
    } else {
      src = i.src1;
    }
    // Shuffle bytes.
    e.vpshufb(i.dest, src, e.GetXmmConstPtr(XMMUnpackSHORT_2));
    // Sign extend words.
    e.vpslld(i.dest, 16);
    e.vpsrad(i.dest, 16);
    // Add 3,3,0,1.
    e.vpaddd(i.dest, e.GetXmmConstPtr(XMM3301));
  }
  static void EmitSHORT_4(X64Emitter& e, const EmitArgType& i) {
    // (VD.x) = 3.0 + (VB.x>>16)*2^-22
    // (VD.y) = 3.0 + (VB.x)*2^-22
    // (VD.z) = 3.0 + (VB.y>>16)*2^-22
    // (VD.w) = 3.0 + (VB.y)*2^-22

    // XMLoadShortN4 plus 3,3,3,3 (for some reason)
    // src is (xx,xx,VALUE,VALUE)
    // (VALUE,VALUE,VALUE,VALUE)
    Xmm src;
    if (i.src1.is_constant) {
      if (i.src1.value->IsConstantZero()) {
        e.vmovdqa(i.dest, e.GetXmmConstPtr(XMM3333));
        return;
      } else {
        // TODO(benvanik): check other common constants/perform shuffle/or here.
        src = e.xmm0;
        e.LoadConstantXmm(src, i.src1.constant());
      }
    } else {
      src = i.src1;
    }
    // Shuffle bytes.
    e.vpshufb(i.dest, src, e.GetXmmConstPtr(XMMUnpackSHORT_4));
    // Sign extend words.
    e.vpslld(i.dest, 16);
    e.vpsrad(i.dest, 16);
    // Add 3,3,3,3.
    e.vpaddd(i.dest, e.GetXmmConstPtr(XMM3333));
  }
  static void EmitUINT_2101010(X64Emitter& e, const EmitArgType& i) {
    assert_always("not implemented");
  }
  static void Emit8_IN_16(X64Emitter& e, const EmitArgType& i, uint32_t flags) {
    assert_false(IsPackOutSaturate(flags));
    if (IsPackToLo(flags)) {
      // Unpack to LO.
      if (IsPackInUnsigned(flags)) {
        if (IsPackOutUnsigned(flags)) {
          // unsigned -> unsigned
          assert_always();
        } else {
          // unsigned -> signed
          assert_always();
        }
      } else {
        if (IsPackOutUnsigned(flags)) {
          // signed -> unsigned
          assert_always();
        } else {
          // signed -> signed
          e.vpshufb(i.dest, i.src1, e.GetXmmConstPtr(XMMByteOrderMask));
          e.vpunpckhbw(i.dest, i.dest, i.dest);
          e.vpsraw(i.dest, 8);
        }
      }
    } else {
      // Unpack to HI.
      if (IsPackInUnsigned(flags)) {
        if (IsPackOutUnsigned(flags)) {
          // unsigned -> unsigned
          assert_always();
        } else {
          // unsigned -> signed
          assert_always();
        }
      } else {
        if (IsPackOutUnsigned(flags)) {
          // signed -> unsigned
          assert_always();
        } else {
          // signed -> signed
          e.vpshufb(i.dest, i.src1, e.GetXmmConstPtr(XMMByteOrderMask));
          e.vpunpcklbw(i.dest, i.dest, i.dest);
          e.vpsraw(i.dest, 8);
        }
      }
    }
  }
  static void Emit16_IN_32(X64Emitter& e, const EmitArgType& i,
                           uint32_t flags) {
    assert_false(IsPackOutSaturate(flags));
    if (IsPackToLo(flags)) {
      // Unpack to LO.
      if (IsPackInUnsigned(flags)) {
        if (IsPackOutUnsigned(flags)) {
          // unsigned -> unsigned
          assert_always();
        } else {
          // unsigned -> signed
          assert_always();
        }
      } else {
        if (IsPackOutUnsigned(flags)) {
          // signed -> unsigned
          assert_always();
        } else {
          // signed -> signed
          e.vpunpckhwd(i.dest, i.src1, i.src1);
          e.vpsrad(i.dest, 16);
        }
      }
    } else {
      // Unpack to HI.
      if (IsPackInUnsigned(flags)) {
        if (IsPackOutUnsigned(flags)) {
          // unsigned -> unsigned
          assert_always();
        } else {
          // unsigned -> signed
          assert_always();
        }
      } else {
        if (IsPackOutUnsigned(flags)) {
          // signed -> unsigned
          assert_always();
        } else {
          // signed -> signed
          e.vpunpcklwd(i.dest, i.src1, i.src1);
          e.vpsrad(i.dest, 16);
        }
      }
    }
    e.vpshufd(i.dest, i.dest, 0xB1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_UNPACK, UNPACK);

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
    e.mov(e.ecx, i.src1.reg().cvt32());
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
    e.mov(e.ecx, i.src1.reg().cvt32());
    e.lock();
    e.cmpxchg(e.qword[e.GetMembaseReg() + e.rcx], i.src3);
    e.sete(i.dest);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ATOMIC_COMPARE_EXCHANGE,
                     ATOMIC_COMPARE_EXCHANGE_I32, ATOMIC_COMPARE_EXCHANGE_I64);

// ============================================================================
// OPCODE_SET_ROUNDING_MODE
// ============================================================================
// Input: FPSCR (PPC format)
static const uint32_t mxcsr_table[] = {
    0x1F80, 0x7F80, 0x5F80, 0x3F80, 0x9F80, 0xFF80, 0xDF80, 0xBF80,
};
struct SET_ROUNDING_MODE_I32
    : Sequence<SET_ROUNDING_MODE_I32,
               I<OPCODE_SET_ROUNDING_MODE, VoidOp, I32Op>> {
  static void Emit(X64Emitter& e, const EmitArgType& i) {
    e.mov(e.rcx, i.src1);
    e.and_(e.rcx, 0x7);
    e.mov(e.rax, uintptr_t(mxcsr_table));
    e.vldmxcsr(e.ptr[e.rax + e.rcx * 4]);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SET_ROUNDING_MODE, SET_ROUNDING_MODE_I32);

void RegisterSequences() {
  Register_OPCODE_COMMENT();
  Register_OPCODE_NOP();
  Register_OPCODE_SOURCE_OFFSET();
  Register_OPCODE_DEBUG_BREAK();
  Register_OPCODE_DEBUG_BREAK_TRUE();
  Register_OPCODE_TRAP();
  Register_OPCODE_TRAP_TRUE();
  Register_OPCODE_CALL();
  Register_OPCODE_CALL_TRUE();
  Register_OPCODE_CALL_INDIRECT();
  Register_OPCODE_CALL_INDIRECT_TRUE();
  Register_OPCODE_CALL_EXTERN();
  Register_OPCODE_RETURN();
  Register_OPCODE_RETURN_TRUE();
  Register_OPCODE_SET_RETURN_ADDRESS();
  Register_OPCODE_BRANCH();
  Register_OPCODE_BRANCH_TRUE();
  Register_OPCODE_BRANCH_FALSE();
  Register_OPCODE_ASSIGN();
  Register_OPCODE_CAST();
  Register_OPCODE_ZERO_EXTEND();
  Register_OPCODE_SIGN_EXTEND();
  Register_OPCODE_TRUNCATE();
  Register_OPCODE_CONVERT();
  Register_OPCODE_ROUND();
  Register_OPCODE_VECTOR_CONVERT_I2F();
  Register_OPCODE_VECTOR_CONVERT_F2I();
  Register_OPCODE_LOAD_VECTOR_SHL();
  Register_OPCODE_LOAD_VECTOR_SHR();
  Register_OPCODE_LOAD_CLOCK();
  Register_OPCODE_LOAD_LOCAL();
  Register_OPCODE_STORE_LOCAL();
  Register_OPCODE_LOAD_CONTEXT();
  Register_OPCODE_STORE_CONTEXT();
  Register_OPCODE_CONTEXT_BARRIER();
  Register_OPCODE_LOAD_MMIO();
  Register_OPCODE_STORE_MMIO();
  Register_OPCODE_LOAD_OFFSET();
  Register_OPCODE_STORE_OFFSET();
  Register_OPCODE_LOAD();
  Register_OPCODE_STORE();
  Register_OPCODE_MEMSET();
  Register_OPCODE_PREFETCH();
  Register_OPCODE_MEMORY_BARRIER();
  Register_OPCODE_MAX();
  Register_OPCODE_VECTOR_MAX();
  Register_OPCODE_MIN();
  Register_OPCODE_VECTOR_MIN();
  Register_OPCODE_SELECT();
  Register_OPCODE_IS_TRUE();
  Register_OPCODE_IS_FALSE();
  Register_OPCODE_IS_NAN();
  Register_OPCODE_COMPARE_EQ();
  Register_OPCODE_COMPARE_NE();
  Register_OPCODE_COMPARE_SLT();
  Register_OPCODE_COMPARE_SLE();
  Register_OPCODE_COMPARE_SGT();
  Register_OPCODE_COMPARE_SGE();
  Register_OPCODE_COMPARE_ULT();
  Register_OPCODE_COMPARE_ULE();
  Register_OPCODE_COMPARE_UGT();
  Register_OPCODE_COMPARE_UGE();
  Register_OPCODE_COMPARE_SLT_FLT();
  Register_OPCODE_COMPARE_SLE_FLT();
  Register_OPCODE_COMPARE_SGT_FLT();
  Register_OPCODE_COMPARE_SGE_FLT();
  Register_OPCODE_COMPARE_ULT_FLT();
  Register_OPCODE_COMPARE_ULE_FLT();
  Register_OPCODE_COMPARE_UGT_FLT();
  Register_OPCODE_COMPARE_UGE_FLT();
  Register_OPCODE_DID_SATURATE();
  Register_OPCODE_VECTOR_COMPARE_EQ();
  Register_OPCODE_VECTOR_COMPARE_SGT();
  Register_OPCODE_VECTOR_COMPARE_SGE();
  Register_OPCODE_VECTOR_COMPARE_UGT();
  Register_OPCODE_VECTOR_COMPARE_UGE();
  Register_OPCODE_ADD();
  Register_OPCODE_ADD_CARRY();
  Register_OPCODE_VECTOR_ADD();
  Register_OPCODE_SUB();
  Register_OPCODE_VECTOR_SUB();
  Register_OPCODE_MUL();
  Register_OPCODE_MUL_HI();
  Register_OPCODE_DIV();
  Register_OPCODE_MUL_ADD();
  Register_OPCODE_MUL_SUB();
  Register_OPCODE_NEG();
  Register_OPCODE_ABS();
  Register_OPCODE_SQRT();
  Register_OPCODE_RSQRT();
  Register_OPCODE_RECIP();
  Register_OPCODE_POW2();
  Register_OPCODE_LOG2();
  Register_OPCODE_DOT_PRODUCT_3();
  Register_OPCODE_DOT_PRODUCT_4();
  Register_OPCODE_AND();
  Register_OPCODE_OR();
  Register_OPCODE_XOR();
  Register_OPCODE_NOT();
  Register_OPCODE_SHL();
  Register_OPCODE_SHR();
  Register_OPCODE_SHA();
  Register_OPCODE_VECTOR_SHL();
  Register_OPCODE_VECTOR_SHR();
  Register_OPCODE_VECTOR_SHA();
  Register_OPCODE_ROTATE_LEFT();
  Register_OPCODE_VECTOR_ROTATE_LEFT();
  Register_OPCODE_VECTOR_AVERAGE();
  Register_OPCODE_BYTE_SWAP();
  Register_OPCODE_CNTLZ();
  Register_OPCODE_INSERT();
  Register_OPCODE_EXTRACT();
  Register_OPCODE_SPLAT();
  Register_OPCODE_PERMUTE();
  Register_OPCODE_SWIZZLE();
  Register_OPCODE_PACK();
  Register_OPCODE_UNPACK();
  Register_OPCODE_ATOMIC_EXCHANGE();
  Register_OPCODE_ATOMIC_COMPARE_EXCHANGE();
  Register_OPCODE_SET_ROUNDING_MODE();
}

bool SelectSequence(X64Emitter* e, const Instr* i, const Instr** new_tail) {
  const InstrKey key(i);
  auto it = sequence_table.find(key);
  if (it != sequence_table.end()) {
    if (it->second(*e, i)) {
      *new_tail = i->next;
      return true;
    }
  }
  XELOGE("No sequence match for variant %s", i->opcode->name);
  return false;
}

}  // namespace x64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
