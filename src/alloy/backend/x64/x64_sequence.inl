/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */


namespace {

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

  operator uint32_t() const {
    return value;
  }

  InstrKey() : value(0) {}
  InstrKey(uint32_t v) : value(v) {}
  InstrKey(const Instr* i) : value(0) {
    opcode = i->opcode->num;
    uint32_t sig = i->opcode->signature;
    dest = GET_OPCODE_SIG_TYPE_DEST(sig) ? OPCODE_SIG_TYPE_V + i->dest->type : 0;
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

  template <Opcode OPCODE,
            KeyType DEST = KEY_TYPE_X,
            KeyType SRC1 = KEY_TYPE_X,
            KeyType SRC2 = KEY_TYPE_X,
            KeyType SRC3 = KEY_TYPE_X>
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
  template <typename T, KeyType KEY_TYPE> friend struct Op;
  template <hir::Opcode OPCODE, typename... Ts> friend struct I;
  void Load(const Instr::Op& op) {}
};

struct OffsetOp : Op<OffsetOp, KEY_TYPE_O> {
  uint64_t value;
protected:
  template <typename T, KeyType KEY_TYPE> friend struct Op;
  template <hir::Opcode OPCODE, typename... Ts> friend struct I;
  void Load(const Instr::Op& op) {
    this->value = op.offset;
  }
};

struct SymbolOp : Op<SymbolOp, KEY_TYPE_S> {
  FunctionInfo* value;
protected:
  template <typename T, KeyType KEY_TYPE> friend struct Op;
  template <hir::Opcode OPCODE, typename... Ts> friend struct I;
  bool Load(const Instr::Op& op) {
    this->value = op.symbol_info;
    return true;
  }
};

struct LabelOp : Op<LabelOp, KEY_TYPE_L> {
  hir::Label* value;
protected:
  template <typename T, KeyType KEY_TYPE> friend struct Op;
  template <hir::Opcode OPCODE, typename... Ts> friend struct I;
  void Load(const Instr::Op& op) {
    this->value = op.label;
  }
};

template <typename T, KeyType KEY_TYPE, typename REG_TYPE, typename CONST_TYPE, int TAG = -1>
struct ValueOp : Op<ValueOp<T, KEY_TYPE, REG_TYPE, CONST_TYPE, TAG>, KEY_TYPE> {
  typedef REG_TYPE reg_type;
  static const int tag = TAG;
  const Value* value;
  bool is_constant;
  virtual bool ConstantFitsIn32Reg() const { return true; }
  const REG_TYPE& reg() const {
    assert_true(!is_constant);
    return reg_;
  }
  operator const REG_TYPE&() const {
    return reg();
  }
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
  bool operator== (const T& b) const {
    return IsEqual(b);
  }
  bool operator!= (const T& b) const {
    return !IsEqual(b);
  }
  bool operator== (const Xbyak::Reg& b) const {
    return IsEqual(b);
  }
  bool operator!= (const Xbyak::Reg& b) const {
    return !IsEqual(b);
  }
  void Load(const Instr::Op& op) {
    const Value* value = op.value;
    this->value = value;
    is_constant = value->IsConstant();
    if (!is_constant) {
      X64Emitter::SetupReg(value, reg_);
    }
  }
protected:
  REG_TYPE reg_;
};

template <int TAG = -1>
struct I8 : ValueOp<I8<TAG>, KEY_TYPE_V_I8, Reg8, int8_t, TAG> {
  typedef ValueOp<I8<TAG>, KEY_TYPE_V_I8, Reg8, int8_t, TAG> BASE;
  const int8_t constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.i8;
  }
};
template <int TAG = -1>
struct I16 : ValueOp<I16<TAG>, KEY_TYPE_V_I16, Reg16, int16_t, TAG> {
  typedef ValueOp<I16<TAG>, KEY_TYPE_V_I16, Reg16, int16_t, TAG> BASE;
  const int16_t constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.i16;
  }
};
template <int TAG = -1>
struct I32 : ValueOp<I32<TAG>, KEY_TYPE_V_I32, Reg32, int32_t, TAG> {
  typedef ValueOp<I32<TAG>, KEY_TYPE_V_I32, Reg32, int32_t, TAG> BASE;
  const int32_t constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.i32;
  }
};
template <int TAG = -1>
struct I64 : ValueOp<I64<TAG>, KEY_TYPE_V_I64, Reg64, int64_t, TAG> {
  typedef ValueOp<I64<TAG>, KEY_TYPE_V_I64, Reg64, int64_t, TAG> BASE;
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
template <int TAG = -1>
struct F32 : ValueOp<F32<TAG>, KEY_TYPE_V_F32, Xmm, float, TAG> {
  typedef ValueOp<F32<TAG>, KEY_TYPE_V_F32, Xmm, float, TAG> BASE;
  const float constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.f32;
  }
};
template <int TAG = -1>
struct F64 : ValueOp<F64<TAG>, KEY_TYPE_V_F64, Xmm, double, TAG> {
  typedef ValueOp<F64<TAG>, KEY_TYPE_V_F64, Xmm, double, TAG> BASE;
  const double constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.f64;
  }
};
template <int TAG = -1>
struct V128 : ValueOp<V128<TAG>, KEY_TYPE_V_V128, Xmm, vec128_t, TAG> {
  typedef ValueOp<V128<TAG>, KEY_TYPE_V_V128, Xmm, vec128_t, TAG> BASE;
  const vec128_t& constant() const {
    assert_true(BASE::is_constant);
    return BASE::value->constant.v128;
  }
};

struct TagTable {
  struct {
    bool valid;
    Instr::Op op;
  } table[16];

  template <typename T, typename std::enable_if<T::key_type == KEY_TYPE_X>::type* = nullptr>
  bool CheckTag(const Instr::Op& op) {
    return true;
  }
  template <typename T, typename std::enable_if<T::key_type == KEY_TYPE_L>::type* = nullptr>
  bool CheckTag(const Instr::Op& op) {
    return true;
  }
  template <typename T, typename std::enable_if<T::key_type == KEY_TYPE_O>::type* = nullptr>
  bool CheckTag(const Instr::Op& op) {
    return true;
  }
  template <typename T, typename std::enable_if<T::key_type == KEY_TYPE_S>::type* = nullptr>
  bool CheckTag(const Instr::Op& op) {
    return true;
  }
  template <typename T, typename std::enable_if<T::key_type >= KEY_TYPE_V_I8>::type* = nullptr>
  bool CheckTag(const Instr::Op& op) {
    const Value* value = op.value;
    if (T::tag == -1) {
      return true;
    }
    if (table[T::tag].valid &&
        table[T::tag].op.value != value) {
      return false;
    }
    table[T::tag].valid = true;
    table[T::tag].op.value = (Value*)value;
    return true;
  }
};

template <typename DEST, typename... Tf>
struct DestField;
template <typename DEST>
struct DestField<DEST> {
  DEST dest;
protected:
  bool LoadDest(const Instr* i, TagTable& tag_table) {
    Instr::Op op;
    op.value = i->dest;
    if (tag_table.CheckTag<DEST>(op)) {
      dest.Load(op);
      return true;
    }
    return false;
  }
};
template <>
struct DestField<VoidOp> {
protected:
  bool LoadDest(const Instr* i, TagTable& tag_table) {
    return true;
  }
};

template <hir::Opcode OPCODE, typename... Ts>
struct I;
template <hir::Opcode OPCODE, typename DEST>
struct I<OPCODE, DEST> : DestField<DEST> {
  typedef DestField<DEST> BASE;
  static const hir::Opcode opcode = OPCODE;
  static const uint32_t key = InstrKey::Construct<OPCODE, DEST::key_type>::value;
  static const KeyType dest_type = DEST::key_type;
  const Instr* instr;
protected:
  template <typename... Ti> friend struct SequenceFields;
  bool Load(const Instr* i, TagTable& tag_table) {
    if (InstrKey(i).value == key &&
        BASE::LoadDest(i, tag_table)) {
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
  static const uint32_t key = InstrKey::Construct<OPCODE, DEST::key_type, SRC1::key_type>::value;
  static const KeyType dest_type = DEST::key_type;
  static const KeyType src1_type = SRC1::key_type;
  const Instr* instr;
  SRC1 src1;
protected:
  template <typename... Ti> friend struct SequenceFields;
  bool Load(const Instr* i, TagTable& tag_table) {
    if (InstrKey(i).value == key &&
        BASE::LoadDest(i, tag_table) &&
        tag_table.CheckTag<SRC1>(i->src1)) {
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
  static const uint32_t key = InstrKey::Construct<OPCODE, DEST::key_type, SRC1::key_type, SRC2::key_type>::value;
  static const KeyType dest_type = DEST::key_type;
  static const KeyType src1_type = SRC1::key_type;
  static const KeyType src2_type = SRC2::key_type;
  const Instr* instr;
  SRC1 src1;
  SRC2 src2;
protected:
  template <typename... Ti> friend struct SequenceFields;
  bool Load(const Instr* i, TagTable& tag_table) {
    if (InstrKey(i).value == key &&
        BASE::LoadDest(i, tag_table) &&
        tag_table.CheckTag<SRC1>(i->src1) &&
        tag_table.CheckTag<SRC2>(i->src2)) {
      instr = i;
      src1.Load(i->src1);
      src2.Load(i->src2);
      return true;
    }
    return false;
  }
};
template <hir::Opcode OPCODE, typename DEST, typename SRC1, typename SRC2, typename SRC3>
struct I<OPCODE, DEST, SRC1, SRC2, SRC3> : DestField<DEST> {
  typedef DestField<DEST> BASE;
  static const hir::Opcode opcode = OPCODE;
  static const uint32_t key = InstrKey::Construct<OPCODE, DEST::key_type, SRC1::key_type, SRC2::key_type, SRC3::key_type>::value;
  static const KeyType dest_type = DEST::key_type;
  static const KeyType src1_type = SRC1::key_type;
  static const KeyType src2_type = SRC2::key_type;
  static const KeyType src3_type = SRC3::key_type;
  const Instr* instr;
  SRC1 src1;
  SRC2 src2;
  SRC3 src3;
protected:
  template <typename... Ti> friend struct SequenceFields;
  bool Load(const Instr* i, TagTable& tag_table) {
    if (InstrKey(i).value == key &&
        BASE::LoadDest(i, tag_table) &&
        tag_table.CheckTag<SRC1>(i->src1) &&
        tag_table.CheckTag<SRC2>(i->src2) &&
        tag_table.CheckTag<SRC3>(i->src3)) {
      instr = i;
      src1.Load(i->src1);
      src2.Load(i->src2);
      src3.Load(i->src3);
      return true;
    }
    return false;
  }
};

template <typename... Ti>
struct SequenceFields;
template <typename I1>
struct SequenceFields<I1> {
  I1 i1;
protected:
  template <typename SEQ, typename... Ti> friend struct Sequence;
  bool Check(const Instr* i, TagTable& tag_table, const Instr** new_tail) {
    if (i1.Load(i, tag_table)) {
      *new_tail = i->next;
      return true;
    }
    return false;
  }
};
template <typename I1, typename I2>
struct SequenceFields<I1, I2> : SequenceFields<I1> {
  I2 i2;
protected:
  template <typename SEQ, typename... Ti> friend struct Sequence;
  bool Check(const Instr* i, TagTable& tag_table, const Instr** new_tail) {
    if (SequenceFields<I1>::Check(i, tag_table, new_tail)) {
      auto ni = i->next;
      if (ni && i2.Load(ni, tag_table)) {
        *new_tail = ni;
        return i;
      }
    }
    return false;
  }
};
template <typename I1, typename I2, typename I3>
struct SequenceFields<I1, I2, I3> : SequenceFields<I1, I2> {
  I3 i3;
protected:
  template <typename SEQ, typename... Ti> friend struct Sequence;
  bool Check(const Instr* i, TagTable& tag_table, const Instr** new_tail) {
    if (SequenceFields<I1, I2>::Check(i, tag_table, new_tail)) {
      auto ni = i->next;
      if (ni && i3.Load(ni, tag_table)) {
        *new_tail = ni;
        return i;
      }
    }
    return false;
  }
};
template <typename I1, typename I2, typename I3, typename I4>
struct SequenceFields<I1, I2, I3, I4> : SequenceFields<I1, I2, I3> {
  I4 i4;
protected:
  template <typename SEQ, typename... Ti> friend struct Sequence;
  bool Check(const Instr* i, TagTable& tag_table, const Instr** new_tail) {
    if (SequenceFields<I1, I2, I3>::Check(i, tag_table, new_tail)) {
      auto ni = i->next;
      if (ni && i4.Load(ni, tag_table)) {
        *new_tail = ni;
        return i;
      }
    }
    return false;
  }
};
template <typename I1, typename I2, typename I3, typename I4, typename I5>
struct SequenceFields<I1, I2, I3, I4, I5> : SequenceFields<I1, I2, I3, I4> {
  I5 i5;
protected:
  template <typename SEQ, typename... Ti> friend struct Sequence;
  bool Check(const Instr* i, TagTable& tag_table, const Instr** new_tail) {
    if (SequenceFields<I1, I2, I3, I4>::Check(i, tag_table, new_tail)) {
      auto ni = i->next;
      if (ni && i5.Load(ni, tag_table)) {
        *new_tail = ni;
        return i;
      }
    }
    return false;
  }
};

template <typename SEQ, typename... Ti>
struct Sequence {
  struct EmitArgs : SequenceFields<Ti...> {};

  static bool Select(X64Emitter& e, const Instr* i, const Instr** new_tail) {
    EmitArgs args;
    TagTable tag_table;
    if (!args.Check(i, tag_table, new_tail)) {
      return false;
    }
    SEQ::Emit(e, args);
    return true;
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
struct SingleSequence : public Sequence<SingleSequence<SEQ, T>, T> {
  typedef Sequence<SingleSequence<SEQ, T>, T> BASE;
  typedef T EmitArgType;
  static const uint32_t head_key = T::key;
  static void Emit(X64Emitter& e, const typename BASE::EmitArgs& _) {
    SEQ::Emit(e, _.i1);
  }

  template <typename REG_FN>
  static void EmitUnaryOp(
      X64Emitter& e, const EmitArgType& i,
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
  static void EmitCommutativeBinaryOp(
      X64Emitter& e, const EmitArgType& i,
      const REG_REG_FN& reg_reg_fn, const REG_CONST_FN& reg_const_fn) {
    if (i.src1.is_constant) {
      assert_true(!i.src2.is_constant);
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
  static void EmitAssociativeBinaryOp(
      X64Emitter& e, const EmitArgType& i,
      const REG_REG_FN& reg_reg_fn, const REG_CONST_FN& reg_const_fn) {
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
  static void EmitCommutativeBinaryXmmOp(
      X64Emitter& e, const EmitArgType& i, const FN& fn) {
    if (i.src1.is_constant) {
      assert_true(!i.src2.is_constant);
      e.LoadConstantXmm(e.xmm0, i.src1.constant());
      fn(e, i.dest, e.xmm0, i.src2);
    } else if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      fn(e, i.dest, i.src1, e.xmm0);
    } else {
      fn(e, i.dest, i.src1, i.src2);
    }
  }

  template <typename FN>
  static void EmitAssociativeBinaryXmmOp(
      X64Emitter& e, const EmitArgType& i, const FN& fn) {
    if (i.src1.is_constant) {
      assert_true(!i.src2.is_constant);
      e.LoadConstantXmm(e.xmm0, i.src1.constant());
      fn(e, i.dest, e.xmm0, i.src2);
    } else if (i.src2.is_constant) {
      e.LoadConstantXmm(e.xmm0, i.src2.constant());
      fn(e, i.dest, i.src1, e.xmm0);
    } else {
      fn(e, i.dest, i.src1, i.src2);
    }
  }

  template <typename REG_REG_FN, typename REG_CONST_FN>
  static void EmitCommutativeCompareOp(
      X64Emitter& e, const EmitArgType& i,
      const REG_REG_FN& reg_reg_fn, const REG_CONST_FN& reg_const_fn) {
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
  static void EmitAssociativeCompareOp(
      X64Emitter& e, const EmitArgType& i,
      const REG_REG_FN& reg_reg_fn, const REG_CONST_FN& reg_const_fn) {
    if (i.src1.is_constant) {
      assert_true(!i.src2.is_constant);
      if (i.src1.ConstantFitsIn32Reg()) {
        reg_const_fn(e, i.dest, i.src2, static_cast<int32_t>(i.src1.constant()), true);
      } else {
        auto temp = GetTempReg<typename decltype(i.src1)::reg_type>(e);
        e.mov(temp, i.src1.constant());
        reg_reg_fn(e, i.dest, i.src2, temp, true);
      }
    } else if (i.src2.is_constant) {
      if (i.src2.ConstantFitsIn32Reg()) {
        reg_const_fn(e, i.dest, i.src1, static_cast<int32_t>(i.src2.constant()), false);
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

static const int ANY = -1;
typedef int tag_t;
static const tag_t TAG0 = 0;
static const tag_t TAG1 = 1;
static const tag_t TAG2 = 2;
static const tag_t TAG3 = 3;
static const tag_t TAG4 = 4;
static const tag_t TAG5 = 5;
static const tag_t TAG6 = 6;
static const tag_t TAG7 = 7;

template <typename T>
void Register() {
  sequence_table.insert({ T::head_key, T::Select });
}
template <typename T, typename Tn, typename... Ts>
void Register() {
  Register<T>();
  Register<Tn, Ts...>();
};
#define EMITTER_OPCODE_TABLE(name, ...) \
  void Register_##name() { \
    Register<__VA_ARGS__>(); \
  }

#define MATCH(...) __VA_ARGS__
#define EMITTER(name, match) struct name : SingleSequence<name, match>
#define SEQUENCE(name, match) struct name : Sequence<name, match>

}  // namespace
