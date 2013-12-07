/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef ALLOY_HIR_VALUE_H_
#define ALLOY_HIR_VALUE_H_

#include <alloy/core.h>
#include <alloy/hir/opcodes.h>


namespace alloy {
namespace hir {


enum TypeName {
  INT8_TYPE,
  INT16_TYPE,
  INT32_TYPE,
  INT64_TYPE,
  FLOAT32_TYPE,
  FLOAT64_TYPE,
  VEC128_TYPE,

  MAX_TYPENAME,
};

enum ValueFlags {
  VALUE_IS_CONSTANT = (1 << 1),
};


class Value {
public:
  uint32_t ordinal;
  TypeName type;

  uint32_t flags;
  union {
    int8_t      i8;
    int16_t     i16;
    int32_t     i32;
    int64_t     i64;
    float       f32;
    double      f64;
    vec128_t    v128;
  } constant;

  void*   tag;

  void set_zero(TypeName type) {
    this->type = type;
    flags |= VALUE_IS_CONSTANT;
    constant.v128.low = constant.v128.high = 0;
  }
  void set_constant(int8_t value) {
    type = INT8_TYPE;
    flags |= VALUE_IS_CONSTANT;
    constant.i8 = value;
  }
  void set_constant(uint8_t value) {
    type = INT8_TYPE;
    flags |= VALUE_IS_CONSTANT;
    constant.i8 = value;
  }
  void set_constant(int16_t value) {
    type = INT16_TYPE;
    flags |= VALUE_IS_CONSTANT;
    constant.i16 = value;
  }
  void set_constant(uint16_t value) {
    type = INT16_TYPE;
    flags |= VALUE_IS_CONSTANT;
    constant.i16 = value;
  }
  void set_constant(int32_t value) {
    type = INT32_TYPE;
    flags |= VALUE_IS_CONSTANT;
    constant.i32 = value;
  }
  void set_constant(uint32_t value) {
    type = INT32_TYPE;
    flags |= VALUE_IS_CONSTANT;
    constant.i32 = value;
  }
  void set_constant(int64_t value) {
    type = INT64_TYPE;
    flags |= VALUE_IS_CONSTANT;
    constant.i64 = value;
  }
  void set_constant(uint64_t value) {
    type = INT64_TYPE;
    flags |= VALUE_IS_CONSTANT;
    constant.i64 = value;
  }
  void set_constant(float value) {
    type = FLOAT32_TYPE;
    flags |= VALUE_IS_CONSTANT;
    constant.f32 = value;
  }
  void set_constant(double value) {
    type = FLOAT64_TYPE;
    flags |= VALUE_IS_CONSTANT;
    constant.f64 = value;
  }
  void set_constant(const vec128_t& value) {
    type = VEC128_TYPE;
    flags |= VALUE_IS_CONSTANT;
    constant.v128 = value;
  }

  inline bool IsConstant() const {
    return !!(flags & VALUE_IS_CONSTANT);
  }
  bool IsConstantTrue() const {
    if (type == VEC128_TYPE) {
      return false;
    }
    return (flags & VALUE_IS_CONSTANT) && !!constant.i64;
  }
  bool IsConstantFalse() const {
    if (type == VEC128_TYPE) {
      return false;
    }
    return (flags & VALUE_IS_CONSTANT) && !constant.i64;
  }
  bool IsConstantZero() const {
    if (type == VEC128_TYPE) {
      return false;
    }
    return (flags & VALUE_IS_CONSTANT) && !constant.i64;
  }
  bool IsConstantEQ(Value* other) const {
    if (type == VEC128_TYPE) {
      return false;
    }
    return (flags & VALUE_IS_CONSTANT) &&
           (other->flags & VALUE_IS_CONSTANT) &&
           constant.i64 == other->constant.i64;
  }
  bool IsConstantNE(Value* other) const {
    if (type == VEC128_TYPE) {
      return false;
    }
    return (flags & VALUE_IS_CONSTANT) &&
           (other->flags & VALUE_IS_CONSTANT) &&
           constant.i64 != other->constant.i64;
  }
  uint64_t AsUint64();

  void Cast(TypeName target_type);
  void ZeroExtend(TypeName target_type);
  void SignExtend(TypeName target_type);
  void Truncate(TypeName target_type);
  void Convert(TypeName target_type, RoundMode round_mode);
  void Round(RoundMode round_mode);
  void Add(Value* other);
  void Sub(Value* other);
  void Mul(Value* other);
  void Div(Value* other);
  void Rem(Value* other);
  static void MulAdd(Value* dest, Value* value1, Value* value2, Value* value3);
  static void MulSub(Value* dest, Value* value1, Value* value2, Value* value3);
  void Neg();
  void Abs();
  void Sqrt();
  void And(Value* other);
  void Or(Value* other);
  void Xor(Value* other);
  void Not();
  void Shl(Value* other);
  void Shr(Value* other);
  void Sha(Value* other);
  void ByteSwap();
  bool Compare(Opcode opcode, Value* other);
};


}  // namespace hir
}  // namespace alloy


#endif  // ALLOY_HIR_VALUE_H_
