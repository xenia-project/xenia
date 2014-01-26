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


Value::Use* Value::AddUse(Arena* arena, Instr* instr) {
  Use* use = arena->Alloc<Use>();
  use->instr = instr;
  use->prev = NULL;
  use->next = use_head;
  if (use_head) {
    use_head->prev = use;
  }
  use_head = use;
  return use;
}

void Value::RemoveUse(Use* use) {
  if (use == use_head) {
    use_head = use->next;
  } else {
    use->prev->next = use->next;
  }
  if (use->next) {
    use->next->prev = use->prev;
  }
}

uint32_t Value::AsUint32() {
  XEASSERT(IsConstant());
  switch (type) {
  case INT8_TYPE:
    return constant.i8;
  case INT16_TYPE:
    return constant.i16;
  case INT32_TYPE:
    return constant.i32;
  case INT64_TYPE:
    return (uint32_t)constant.i64;
  default:
    XEASSERTALWAYS();
    return 0;
  }
}

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
  switch (type) {
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
  // Unsupported types.
  XEASSERTALWAYS();
}
  
void Value::SignExtend(TypeName target_type) {
  switch (type) {
  case INT8_TYPE:
    type = target_type;
    switch (target_type) {
    case INT16_TYPE:
      constant.i16 = constant.i8;
      break;
    case INT32_TYPE:
      constant.i32 = constant.i8;
      break;
    case INT64_TYPE:
      constant.i64 = constant.i8;
      break;
    }
    return;
  case INT16_TYPE:
    type = target_type;
    switch (target_type) {
    case INT32_TYPE:
      constant.i32 = constant.i16;
      break;
    case INT64_TYPE:
      constant.i64 = constant.i16;
      break;
    }
    return;
  case INT32_TYPE:
    type = target_type;
    switch (target_type) {
    case INT64_TYPE:
      constant.i64 = constant.i32;
      break;
    }
    return;
  }
  // Unsupported types.
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
  XEASSERT(type == other->type);
  switch (type) {
  case INT8_TYPE:
    constant.i8 += other->constant.i8;
    break;
  case INT16_TYPE:
    constant.i16 += other->constant.i16;
    break;
  case INT32_TYPE:
    constant.i32 += other->constant.i32;
    break;
  case INT64_TYPE:
    constant.i64 += other->constant.i64;
    break;
  case FLOAT32_TYPE:
    constant.f32 += other->constant.f32;
    break;
  case FLOAT64_TYPE:
    constant.f64 += other->constant.f64;
    break;
  default:
    XEASSERTALWAYS();
    break;
  }
}

void Value::Sub(Value* other) {
  XEASSERT(type == other->type);
  switch (type) {
  case INT8_TYPE:
    constant.i8 -= other->constant.i8;
    break;
  case INT16_TYPE:
    constant.i16 -= other->constant.i16;
    break;
  case INT32_TYPE:
    constant.i32 -= other->constant.i32;
    break;
  case INT64_TYPE:
    constant.i64 -= other->constant.i64;
    break;
  case FLOAT32_TYPE:
    constant.f32 -= other->constant.f32;
    break;
  case FLOAT64_TYPE:
    constant.f64 -= other->constant.f64;
    break;
  default:
    XEASSERTALWAYS();
    break;
  }
}

void Value::Mul(Value* other) {
  XEASSERT(type == other->type);
  switch (type) {
  case INT8_TYPE:
    constant.i8 *= other->constant.i8;
    break;
  case INT16_TYPE:
    constant.i16 *= other->constant.i16;
    break;
  case INT32_TYPE:
    constant.i32 *= other->constant.i32;
    break;
  case INT64_TYPE:
    constant.i64 *= other->constant.i64;
    break;
  case FLOAT32_TYPE:
    constant.f32 *= other->constant.f32;
    break;
  case FLOAT64_TYPE:
    constant.f64 *= other->constant.f64;
    break;
  default:
    XEASSERTALWAYS();
    break;
  }
}

void Value::Div(Value* other) {
  XEASSERT(type == other->type);
  switch (type) {
  case INT8_TYPE:
    constant.i8 /= other->constant.i8;
    break;
  case INT16_TYPE:
    constant.i16 /= other->constant.i16;
    break;
  case INT32_TYPE:
    constant.i32 /= other->constant.i32;
    break;
  case INT64_TYPE:
    constant.i64 /= other->constant.i64;
    break;
  case FLOAT32_TYPE:
    constant.f32 /= other->constant.f32;
    break;
  case FLOAT64_TYPE:
    constant.f64 /= other->constant.f64;
    break;
  default:
    XEASSERTALWAYS();
    break;
  }
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
  switch (type) {
  case INT8_TYPE:
    constant.i8 = -constant.i8;
    break;
  case INT16_TYPE:
    constant.i16 = -constant.i16;
    break;
  case INT32_TYPE:
    constant.i32 = -constant.i32;
    break;
  case INT64_TYPE:
    constant.i64 = -constant.i64;
    break;
  case FLOAT32_TYPE:
    constant.f32 = -constant.f32;
    break;
  case FLOAT64_TYPE:
    constant.f64 = -constant.f64;
    break;
  default:
    XEASSERTALWAYS();
    break;
  }
}

void Value::Abs() {
  switch (type) {
  case INT8_TYPE:
    constant.i8 = abs(constant.i8);
    break;
  case INT16_TYPE:
    constant.i16 = abs(constant.i16);
    break;
  case INT32_TYPE:
    constant.i32 = abs(constant.i32);
    break;
  case INT64_TYPE:
    constant.i64 = abs(constant.i64);
    break;
  case FLOAT32_TYPE:
    constant.f32 = abs(constant.f32);
    break;
  case FLOAT64_TYPE:
    constant.f64 = abs(constant.f64);
    break;
  default:
    XEASSERTALWAYS();
    break;
  }
}

void Value::Sqrt() {
  switch (type) {
  case FLOAT32_TYPE:
    constant.f32 = 1.0f / sqrtf(constant.f32);
    break;
  case FLOAT64_TYPE:
    constant.f64 = 1.0 / sqrt(constant.f64);
    break;
  default:
    XEASSERTALWAYS();
    break;
  }
}

void Value::RSqrt() {
  switch (type) {
  case FLOAT32_TYPE:
    constant.f32 = sqrt(constant.f32);
    break;
  case FLOAT64_TYPE:
    constant.f64 = sqrt(constant.f64);
    break;
  default:
    XEASSERTALWAYS();
    break;
  }
}

void Value::And(Value* other) {
  XEASSERT(type == other->type);
  switch (type) {
  case INT8_TYPE:
    constant.i8 &= other->constant.i8;
    break;
  case INT16_TYPE:
    constant.i16 &= other->constant.i16;
    break;
  case INT32_TYPE:
    constant.i32 &= other->constant.i32;
    break;
  case INT64_TYPE:
    constant.i64 &= other->constant.i64;
    break;
  default:
    XEASSERTALWAYS();
    break;
  }
}

void Value::Or(Value* other) {
  XEASSERT(type == other->type);
  switch (type) {
  case INT8_TYPE:
    constant.i8 |= other->constant.i8;
    break;
  case INT16_TYPE:
    constant.i16 |= other->constant.i16;
    break;
  case INT32_TYPE:
    constant.i32 |= other->constant.i32;
    break;
  case INT64_TYPE:
    constant.i64 |= other->constant.i64;
    break;
  default:
    XEASSERTALWAYS();
    break;
  }
}

void Value::Xor(Value* other) {
  XEASSERT(type == other->type);
  switch (type) {
  case INT8_TYPE:
    constant.i8 ^= other->constant.i8;
    break;
  case INT16_TYPE:
    constant.i16 ^= other->constant.i16;
    break;
  case INT32_TYPE:
    constant.i32 ^= other->constant.i32;
    break;
  case INT64_TYPE:
    constant.i64 ^= other->constant.i64;
    break;
  default:
    XEASSERTALWAYS();
    break;
  }
}

void Value::Not() {
  switch (type) {
  case INT8_TYPE:
    constant.i8 = ~constant.i8;
    break;
  case INT16_TYPE:
    constant.i16 = ~constant.i16;
    break;
  case INT32_TYPE:
    constant.i32 = ~constant.i32;
    break;
  case INT64_TYPE:
    constant.i64 = ~constant.i64;
    break;
  case VEC128_TYPE:
    constant.v128.low = ~constant.v128.low;
    constant.v128.high = ~constant.v128.high;
    break;
  default:
    XEASSERTALWAYS();
    break;
  }
}

void Value::Shl(Value* other) {
  XEASSERT(other->type == INT8_TYPE);
  switch (type) {
  case INT8_TYPE:
    constant.i8 <<= other->constant.i8;
    break;
  case INT16_TYPE:
    constant.i16 <<= other->constant.i8;
    break;
  case INT32_TYPE:
    constant.i32 <<= other->constant.i8;
    break;
  case INT64_TYPE:
    constant.i64 <<= other->constant.i8;
    break;
  default:
    XEASSERTALWAYS();
    break;
  }
}

void Value::Shr(Value* other) {
  XEASSERT(other->type == INT8_TYPE);
  switch (type) {
  case INT8_TYPE:
    constant.i8 = (uint8_t)constant.i8 >> other->constant.i8;
    break;
  case INT16_TYPE:
    constant.i16 = (uint16_t)constant.i16 >> other->constant.i8;
    break;
  case INT32_TYPE:
    constant.i32 = (uint32_t)constant.i32 >> other->constant.i8;
    break;
  case INT64_TYPE:
    constant.i64 = (uint16_t)constant.i64 >> other->constant.i8;
    break;
  default:
    XEASSERTALWAYS();
    break;
  }
}

void Value::Sha(Value* other) {
  XEASSERT(other->type == INT8_TYPE);
  switch (type) {
  case INT8_TYPE:
    constant.i8 = constant.i8 >> other->constant.i8;
    break;
  case INT16_TYPE:
    constant.i16 = constant.i16 >> other->constant.i8;
    break;
  case INT32_TYPE:
    constant.i32 = constant.i32 >> other->constant.i8;
    break;
  case INT64_TYPE:
    constant.i64 = constant.i64 >> other->constant.i8;
    break;
  default:
    XEASSERTALWAYS();
    break;
  }
}

void Value::ByteSwap() {
  switch (type) {
  case INT8_TYPE:
    constant.i8 = constant.i8;
    break;
  case INT16_TYPE:
    constant.i16 = XESWAP16(constant.i16);
    break;
  case INT32_TYPE:
    constant.i32 = XESWAP32(constant.i32);
    break;
  case INT64_TYPE:
    constant.i64 = XESWAP64(constant.i64);
    break;
  case VEC128_TYPE:
    for (int n = 0; n < 4; n++) {
      constant.v128.i4[n] = XESWAP32(constant.v128.i4[n]);
    }
    break;
  default:
    XEASSERTALWAYS();
    break;
  }
}

bool Value::Compare(Opcode opcode, Value* other) {
  // TODO(benvanik): big matrix.
  XEASSERTALWAYS();
  return false;
}
