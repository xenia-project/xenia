/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/backend/x64/lowering/lowering_sequences.h>

#include <alloy/backend/x64/lowering/lowering_table.h>

#include <tuple>

using namespace alloy;
using namespace alloy::backend::x64;
using namespace alloy::backend::x64::lir;
using namespace alloy::backend::x64::lowering;
using namespace alloy::hir;

using namespace std;


namespace {


enum ArgType {
  NULL_ARG_TYPE,
  VALUE_ARG_TYPE,
  CONSTANT_ARG_TYPE,
  OUT_ARG_TYPE,
  IN_ARG_TYPE,
  REG_ARG_TYPE,
};
template<ArgType ARG_TYPE>
class Arg {
public:
  const static ArgType arg_type = ARG_TYPE;
};

class NullArg : public Arg<NULL_ARG_TYPE> {};

template<int TYPE>
class ValueRef : public Arg<VALUE_ARG_TYPE> {
public:
  enum { type = TYPE };
};
template<int TYPE, bool HAS_VALUE = false,
    int8_t INT8_VALUE = 0,
    int16_t INT16_VALUE = 0,
    int32_t INT32_VALUE = 0,
    int64_t INT64_VALUE = 0,
    intmax_t FLOAT32_NUM_VALUE = 0,
    intmax_t FLOAT32_DEN_VALUE = 1,
    intmax_t FLOAT64_NUM_VALUE = 0,
    intmax_t FLOAT64_DEN_VALUE = 1>
    // vec
class Constant : public Arg<CONSTANT_ARG_TYPE> {
public:
  enum { type = TYPE };
  enum : bool { has_value = HAS_VALUE };
  const static int8_t i8 = INT8_VALUE;
  const static int16_t i16 = INT16_VALUE;
  const static int32_t i32 = INT32_VALUE;
  const static int64_t i64 = INT64_VALUE;
  const static intmax_t f32_num = FLOAT32_NUM_VALUE;
  const static intmax_t f32_den = FLOAT32_DEN_VALUE;
  const static intmax_t f64_num = FLOAT64_NUM_VALUE;
  const static intmax_t f64_den = FLOAT64_DEN_VALUE;
  // vec
};
template<int8_t VALUE>
class Int8Constant : public Constant<INT8_TYPE, true, VALUE> {};
template<int16_t VALUE>
class Int16Constant : public Constant<INT16_TYPE, true, 0, VALUE> {};
template<int32_t VALUE>
class Int32Constant : public Constant<INT32_TYPE, true, 0, 0, VALUE> {};
template<int64_t VALUE>
class Int64Constant : public Constant<INT64_TYPE, true, 0, 0, 0, VALUE> {};
template<intmax_t NUM, intmax_t DEN>
class Float32Constant : public Constant<FLOAT32_TYPE, true, 0, 0, 0, 0, NUM, DEN> {};
template<intmax_t NUM, intmax_t DEN>
class Float64Constant : public Constant<FLOAT64_TYPE, true, 0, 0, 0, 0, 0, 0, NUM, DEN> {};
// vec

template<int SLOT>
class Out : public Arg<OUT_ARG_TYPE> {
public:
  enum { slot = SLOT };
};

template<int SLOT, int ARG>
class In : public Arg<IN_ARG_TYPE> {
public:
  enum { slot = SLOT };
  enum { arg = ARG };
};

template<int REG_NAME>
class Reg : public Arg<REG_ARG_TYPE> {
public:
  enum { reg_name = REG_NAME };
};


class Matcher {
public:
  template <int v> struct IntType { static const int value = v; };

  template <typename T>
  static bool CheckArg(Instr* instr_list[], Instr::Op& op, IntType<NULL_ARG_TYPE>) {
    return true;
  }
  template <typename T>
  static bool CheckArg(Instr* instr_list[], Instr::Op& op, IntType<VALUE_ARG_TYPE>) {
    return op.value->type == T::type;
  }
  template <typename T>
  static bool CheckArg(Instr* instr_list[], Instr::Op& op, IntType<CONSTANT_ARG_TYPE>) {
    if (op.value->type != T::type) {
      return false;
    }
    if (!T::has_value) {
      return true;
    }
    switch (T::type) {
    case INT8_TYPE:
      return op.value->constant.i8 == T::i8;
    case INT16_TYPE:
      return op.value->constant.i16 == T::i16;
    case INT32_TYPE:
      return op.value->constant.i32 == T::i32;
    case INT64_TYPE:
      return op.value->constant.i64 == T::i64;
    case FLOAT32_TYPE:
      return op.value->constant.f32 == (T::f32_num / T::f32_den);
    case FLOAT64_TYPE:
      return op.value->constant.f64 == (T::f64_num / T::f64_den);
    // vec
    }
    return false;
  }
  template <typename T>
  static bool CheckArg(Instr* instr_list[], Instr::Op& op, IntType<OUT_ARG_TYPE>) {
    Instr* instr = instr_list[T::slot];
    return op.value == instr->dest;
  }
  template <typename T>
  static bool CheckArg(Instr* instr_list[], Instr::Op& op, IntType<IN_ARG_TYPE>) {
    Instr* instr = instr_list[T::slot];
    switch (T::arg) {
    case 0:
      return op.value == instr->src1.value;
    case 1:
      return op.value == instr->src2.value;
    case 2:
      return op.value == instr->src3.value;
    }
    return false;
  }

  template <typename T>
  static bool CheckDest(Instr* instr_list[], Value* value, IntType<NULL_ARG_TYPE>) {
    return true;
  }
  template <typename T>
  static bool CheckDest(Instr* instr_list[], Value* value, IntType<VALUE_ARG_TYPE>) {
    return value->type == T::type;
  }
  template <typename T>
  static bool CheckDest(Instr* instr_list[], Value* value, IntType<OUT_ARG_TYPE>) {
    Instr* instr = instr_list[T::slot];
    return value == instr->dest;
  }
  template <typename T>
  static bool CheckDest(Instr* instr_list[], Value* value, IntType<IN_ARG_TYPE>) {
    Instr* instr = instr_list[T::slot];
    switch (T::arg) {
    case 0:
      return value == instr->src1.value;
    case 1:
      return value == instr->src2.value;
    case 2:
      return value == instr->src3.value;
    }
    return false;
  }

  template <size_t I = 0, typename HIRS>
  static typename std::enable_if<I == std::tuple_size<HIRS>::value, void>::type
      Match(Instr* instr_list[], int& instr_offset, Instr*&, bool&) {
  }
  template <size_t I = 0, typename HIRS>
  static typename std::enable_if<I < std::tuple_size<HIRS>::value, void>::type
      Match(Instr* instr_list[], int& instr_offset, Instr*& instr, bool& matched) {
    instr_list[instr_offset++] = instr;
    typedef std::tuple_element<I, HIRS>::type T;
    if (instr->opcode->num != T::opcode) {
      matched = false;
      return;
    }
    // Matches opcode, check args.
    if (!std::is_same<T::dest, NullArg>::value) {
      if (!CheckDest<T::dest>(instr_list, instr->dest, IntType<T::dest::arg_type>())) {
        matched = false;
        return;
      }
    }
    if (!std::is_same<T::arg0, NullArg>::value) {
      if (!CheckArg<T::arg0>(instr_list, instr->src1, IntType<T::arg0::arg_type>())) {
        matched = false;
        return;
      }
    }
    if (!std::is_same<T::arg1, NullArg>::value) {
      if (!CheckArg<T::arg1>(instr_list, instr->src2, IntType<T::arg1::arg_type>())) {
        matched = false;
        return;
      }
    }
    if (!std::is_same<T::arg2, NullArg>::value) {
      if (!CheckArg<T::arg2>(instr_list, instr->src3, IntType<T::arg2::arg_type>())) {
        matched = false;
        return;
      }
    }
    instr = instr->next;
    Match<I + 1, HIRS>(instr_list, instr_offset, instr, matched);
  }

  template<typename HIRS>
  static bool Matches(Instr* instr_list[], int& instr_offset, Instr*& instr) {
    bool matched = true;
    Instr* orig_instr = instr;
    Match<0, HIRS>(instr_list, instr_offset, instr, matched);
    if (!matched) {
      instr = orig_instr;
    }
    return matched;
  }
};


class Emitter {
public:
  template <size_t I = 0, typename LIRS>
  static typename std::enable_if<I == std::tuple_size<LIRS>::value, void>::type
      EmitInstr(LIRBuilder* builder, Instr*[]) {
  }
  template <size_t I = 0, typename LIRS>
  static typename std::enable_if<I < std::tuple_size<LIRS>::value, void>::type
      EmitInstr(LIRBuilder* builder, Instr* instr_list[]) {
    typedef std::tuple_element<I, LIRS>::type T;
    //LIRInstr* lir_instr = builder->AppendInstr(T::opcode);
    if (!std::is_same<T::dest, NullArg>::value) {
      // lir_instr->dest = ...
    }
    if (!std::is_same<T::arg0, NullArg>::value) {
      //
    }
    if (!std::is_same<T::arg1, NullArg>::value) {
      //
    }
    if (!std::is_same<T::arg2, NullArg>::value) {
      //
    }
    EmitInstr<I + 1, LIRS>(builder, instr_list);
  }

  template <typename LIRS>
  static void Emit(LIRBuilder* builder, Instr* instr_list[]) {
    EmitInstr<0, LIRS>(builder, instr_list);
  }
};


template<typename MATCH, typename EMIT>
void Translate(LoweringTable* table) {
  auto exec = [](LIRBuilder* builder, Instr*& instr) {
    Instr* instr_list[32] = { 0 };
    int instr_offset = 0;
    if (Matcher::Matches<MATCH>(instr_list, instr_offset, instr)) {
      Emitter::Emit<EMIT>(builder, instr_list);
      return true;
    }
    return false;
  };
  auto new_fn = new LoweringTable::TypedFnWrapper<decltype(exec)>(exec);
  table->AddSequence(std::tuple_element<0, MATCH>::type::opcode, new_fn);
}


}  // namespace


namespace {
template<Opcode OPCODE, OpcodeSignature SIGNATURE, int FLAGS,
         typename DEST = NullArg, typename ARG0 = NullArg, typename ARG1 = NullArg, typename ARG2 = NullArg>
class HIR_OPCODE {
public:
  static const Opcode opcode = OPCODE;
  static const OpcodeSignature signature = SIGNATURE;
  static const int flags = FLAGS;

  typedef DEST dest;
  typedef ARG0 arg0;
  typedef ARG1 arg1;
  typedef ARG2 arg2;
};
#define DEFINE_OPCODE(num, name, sig, flags) \
    template<typename DEST = NullArg, typename ARG0 = NullArg, typename ARG1 = NullArg, typename ARG2 = NullArg> \
class BASE_HIR_##num : public HIR_OPCODE<num, sig, flags, DEST, ARG0, ARG1, ARG2>{};
#include <alloy/hir/opcodes.inl>
#undef DEFINE_OPCODE
#define DEFINE_OPCODE_V_O(name) \
    template<typename DEST, typename ARG0> \
    class HIR_##name : public BASE_HIR_OPCODE_##name<DEST, ARG0> {}
#define DEFINE_OPCODE_V_O_V(name) \
    template<typename DEST, typename ARG0, typename ARG1> \
    class HIR_##name : public BASE_HIR_OPCODE_##name<DEST, ARG0, ARG1> {}
#define DEFINE_OPCODE_V_V(name) \
    template<typename DEST, typename ARG0> \
    class HIR_##name : public BASE_HIR_OPCODE_##name<DEST, ARG0> {}
#define DEFINE_OPCODE_V_V_O(name) \
    template<typename DEST, typename ARG0, typename ARG1> \
    class HIR_##name : public BASE_HIR_OPCODE_##name<DEST, ARG0, ARG1> {}
#define DEFINE_OPCODE_V_V_V(name) \
    template<typename DEST, typename ARG0, typename ARG1> \
    class HIR_##name : public BASE_HIR_OPCODE_##name<DEST, ARG0, ARG1> {}
#define DEFINE_OPCODE_V_V_V_V(name) \
    template<typename DEST, typename ARG0, typename ARG1, typename ARG2> \
    class HIR_##name : public BASE_HIR_OPCODE_##name<DEST, ARG0, ARG1, ARG2> {}
#define DEFINE_OPCODE_X(name) \
    template<int NONE = 0> \
    class HIR_##name : public BASE_HIR_OPCODE_##name<> {}
#define DEFINE_OPCODE_X_L(name) \
    template<typename ARG0> \
    class HIR_##name : public BASE_HIR_OPCODE_##name<NullArg, ARG0> {}
#define DEFINE_OPCODE_X_O(name) \
    template<typename ARG0> \
    class HIR_##name : public BASE_HIR_OPCODE_##name<NullArg, ARG0> {}
#define DEFINE_OPCODE_X_S(name) \
    template<typename ARG0> \
    class HIR_##name : public BASE_HIR_OPCODE_##name<NullArg, ARG0> {}
#define DEFINE_OPCODE_X_V(name) \
    template<typename ARG0> \
    class HIR_##name : public BASE_HIR_OPCODE_##name<NullArg, ARG0> {}
#define DEFINE_OPCODE_X_V_L(name) \
    template<typename ARG0, typename ARG1> \
    class HIR_##name : public BASE_HIR_OPCODE_##name<NullArg, ARG0, ARG1> {}
#define DEFINE_OPCODE_X_V_O(name) \
    template<typename ARG0, typename ARG1> \
    class HIR_##name : public BASE_HIR_OPCODE_##name<NullArg, ARG0, ARG1> {}
#define DEFINE_OPCODE_X_V_S(name) \
    template<typename ARG0, typename ARG1> \
    class HIR_##name : public BASE_HIR_OPCODE_##name<NullArg, ARG0, ARG1> {}
#define DEFINE_OPCODE_X_V_V(name) \
    template<typename ARG0, typename ARG1> \
    class HIR_##name : public BASE_HIR_OPCODE_##name<NullArg, ARG0, ARG1> {}
#include <alloy/backend/x64/lowering/lowering_hir_opcodes.inl>
}

namespace {
template<LIROpcode OPCODE, LIROpcodeSignature SIGNATURE, int FLAGS,
         typename DEST = NullArg, typename ARG0 = NullArg, typename ARG1 = NullArg, typename ARG2 = NullArg>
class LIR_OPCODE {
public:
  static const LIROpcode opcode = OPCODE;
  static const LIROpcodeSignature signature = SIGNATURE;
  static const int flags = FLAGS;

  typedef DEST dest;
  typedef ARG0 arg0;
  typedef ARG1 arg1;
  typedef ARG2 arg2;
};
#define DEFINE_OPCODE(num, name, sig, flags) \
    template<typename DEST = NullArg, typename ARG0 = NullArg, typename ARG1 = NullArg, typename ARG2 = NullArg> \
    class BASE_##num : public LIR_OPCODE<num, sig, flags, DEST, ARG0, ARG1, ARG2> {};
#include <alloy/backend/x64/lir/lir_opcodes.inl>
#undef DEFINE_OPCODE
#define DEFINE_LIR_OPCODE_X(name) \
    template<int NONE = 0> \
    class LIR_##name : public BASE_LIR_OPCODE_##name<> {}
#define DEFINE_LIR_OPCODE_R32(name) \
    template<typename DEST> \
    class LIR_##name : public BASE_LIR_OPCODE_##name<DEST> {}
#define DEFINE_LIR_OPCODE_R32_R32(name) \
    template<typename DEST, typename ARG0> \
    class LIR_##name : public BASE_LIR_OPCODE_##name<DEST, ARG0> {}
#define DEFINE_LIR_OPCODE_R32_R32_C32(name) \
    template<typename DEST, typename ARG0, typename ARG1> \
    class LIR_##name : public BASE_LIR_OPCODE_##name<DEST, ARG0, ARG1> {}
#include <alloy/backend/x64/lowering/lowering_lir_opcodes.inl>
}

enum {
  REG8, REG16, REG32, REG64, REGXMM,

  AL,   AX,   EAX,  RAX,
  BL,   BX,   EBX,  RBX,
  CL,   CX,   ECX,  RCX,
  DL,   DX,   EDX,  RDX,
  R8B,  R8W,  R8D,   R8,
  R9B,  R9W,  R9D,   R9,
  R10B, R10W, R10D, R10,
  R11B, R11W, R11D, R11,
  R12B, R12W, R12D, R12,
  R13B, R13W, R13D, R13,
  R14B, R14W, R14D, R14,
  R15B, R15W, R15D, R15,

  SIL,  SI,   ESI,  RSI,
  DIL,  DI,   EDI,  RDI,

  RBP,
  RSP,

  XMM0,
  XMM1,
  XMM2,
  XMM3,
  XMM4,
  XMM5,
  XMM6,
  XMM7,
  XMM8,
  XMM9,
  XMM10,
  XMM11,
  XMM12,
  XMM13,
  XMM14,
  XMM15,
};


void alloy::backend::x64::lowering::RegisterSequences(LoweringTable* table) {
  Translate<tuple<
    HIR_NOP<>
  >, tuple<
  >> (table);

  Translate<tuple<
    HIR_SUB<ValueRef<INT32_TYPE>, ValueRef<INT32_TYPE>, ValueRef<INT32_TYPE>>
  >, tuple<
    LIR_MOV_I32<Out<0>, In<0, 0>>,
    LIR_SUB_I32<Out<0>, In<0, 1>>
  >> (table);
  Translate<tuple<
    HIR_SUB<ValueRef<INT32_TYPE>, ValueRef<INT32_TYPE>, Int32Constant<1>>
  >, tuple<
    LIR_MOV_I32<Out<0>, In<0, 0>>,
    LIR_DEC_I32<Out<0>>
  >> (table);

  Translate<tuple<
    HIR_MUL<ValueRef<INT32_TYPE>, ValueRef<INT32_TYPE>, Constant<INT32_TYPE>>
  >, tuple<
    LIR_IMUL_I32_AUX<Out<0>, In<0, 0>, In<0, 1>>
  >> (table);
  Translate<tuple<
    HIR_MUL<ValueRef<INT32_TYPE>, ValueRef<INT32_TYPE>, ValueRef<INT32_TYPE>>
  >, tuple<
    LIR_MOV_I32<Out<0>, In<0, 0>>,
    LIR_IMUL_I32<Out<0>, In<0, 1>>
  >> (table);

  Translate<tuple<
    HIR_DIV<ValueRef<INT32_TYPE>, ValueRef<INT32_TYPE>, ValueRef<INT32_TYPE>>
  >, tuple<
    LIR_MOV_I32<Reg<EAX>, In<0, 0>>,
    LIR_XOR_I32<Reg<EDX>, Reg<EDX>>,
    LIR_DIV_I32<In<0, 1>>,
    LIR_MOV_I32<Out<0>, Reg<EAX>>
  >> (table);

  //Translate<tuple<
  //  HIR_SUB<ValueRef<INT32_TYPE>, ValueRef<INT32_TYPE>, ValueRef<INT32_TYPE>>,
  //  HIR_IS_TRUE<ValueRef<INT8_TYPE>, Out<0>>
  //>, tuple<
  //  LIR_MOV_I32<Out<0>, In<0, 0>>,
  //  LIR_SUB_I32<Out<0>, In<0, 1>>,
  //  LIR_EFLAGS_NOT_ZF<Out<1>>
  //>> (table);
}
