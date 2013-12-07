/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <alloy/hir/value.h>

using namespace alloy;
using namespace alloy::hir;


uint64_t Value::AsUint64() {
  XEASSERT(IsConstant());
  switch (type) {
  case INT8_TYPE:
    return constant.i8;
  case INT16_TYPE:
    return constant.i16;
  case INT32_TYPE:
    return constant.i32;
  case INT64_TYPE:
    return constant.i64;
  default:
    XEASSERTALWAYS();
    return 0;
  }
}

void Value::Cast(TypeName target_type) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::ZeroExtend(TypeName target_type) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}
  
void Value::SignExtend(TypeName target_type) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::Truncate(TypeName target_type) {
  switch (type) {
  case INT16_TYPE:
    switch (target_type) {
    case INT8_TYPE:
      type = target_type;
      constant.i64 = constant.i64 & 0xFF;
      return;
    }
    break;
  case INT32_TYPE:
    switch (target_type) {
    case INT8_TYPE:
      type = target_type;
      constant.i64 = constant.i64 & 0xFF;
      return;
    case INT16_TYPE:
      type = target_type;
      constant.i64 = constant.i64 & 0xFFFF;
      return;
    }
    break;
  case INT64_TYPE:
    switch (target_type) {
    case INT8_TYPE:
      type = target_type;
      constant.i64 = constant.i64 & 0xFF;
      return;
    case INT16_TYPE:
      type = target_type;
      constant.i64 = constant.i64 & 0xFFFF;
      return;
    case INT32_TYPE:
      type = target_type;
      constant.i64 = constant.i64 & 0xFFFFFFFF;
      return;
    }
    break;
  }
  // Unsupported types.
  XEASSERTALWAYS();
}

void Value::Convert(TypeName target_type, RoundMode round_mode) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::Round(RoundMode round_mode) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::Add(Value* other) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::Sub(Value* other) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::Mul(Value* other) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::Div(Value* other) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::Rem(Value* other) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::MulAdd(Value* dest, Value* value1, Value* value2, Value* value3) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::MulSub(Value* dest, Value* value1, Value* value2, Value* value3) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::Neg() {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::Abs() {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::Sqrt() {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::And(Value* other) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::Or(Value* other) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::Xor(Value* other) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::Not() {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::Shl(Value* other) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::Shr(Value* other) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::Sha(Value* other) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

void Value::ByteSwap() {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
}

bool Value::Compare(Opcode opcode, Value* other) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
  return false;
}
