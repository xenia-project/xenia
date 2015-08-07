/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/math.h"

namespace xe {

// TODO(benvanik): replace with alternate implementation.
// XMConvertFloatToHalf
// Copyright (c) Microsoft Corporation. All rights reserved.
uint16_t float_to_half(float value) {
  uint32_t Result;
  uint32_t IValue = (reinterpret_cast<uint32_t*>(&value))[0];
  uint32_t Sign = (IValue & 0x80000000U) >> 16U;
  IValue = IValue & 0x7FFFFFFFU;  // Hack off the sign
  if (IValue > 0x47FFEFFFU) {
    // The number is too large to be represented as a half. Saturate to
    // infinity.
    Result = 0x7FFFU;
  } else {
    if (IValue < 0x38800000U) {
      // The number is too small to be represented as a normalized half.
      // Convert it to a denormalized value.
      uint32_t Shift = 113U - (IValue >> 23U);
      IValue = (0x800000U | (IValue & 0x7FFFFFU)) >> Shift;
    } else {
      // Rebias the exponent to represent the value as a normalized half.
      IValue += 0xC8000000U;
    }
    Result = ((IValue + 0x0FFFU + ((IValue >> 13U) & 1U)) >> 13U) & 0x7FFFU;
  }
  return (uint16_t)(Result | Sign);
}

// TODO(benvanik): replace with alternate implementation.
// XMConvertHalfToFloat
// Copyright (c) Microsoft Corporation. All rights reserved.
float half_to_float(uint16_t value) {
  uint32_t Mantissa = (uint32_t)(value & 0x03FF);
  uint32_t Exponent;
  if ((value & 0x7C00) != 0) {
    // The value is normalized
    Exponent = (uint32_t)((value >> 10) & 0x1F);
  } else if (Mantissa != 0) {
    // The value is denormalized
    // Normalize the value in the resulting float
    Exponent = 1;
    do {
      Exponent--;
      Mantissa <<= 1;
    } while ((Mantissa & 0x0400) == 0);
    Mantissa &= 0x03FF;
  } else {
    // The value is zero
    Exponent = (uint32_t)-112;
  }
  uint32_t Result = ((value & 0x8000) << 16) |  // Sign
                    ((Exponent + 112) << 23) |  // Exponent
                    (Mantissa << 13);           // Mantissa
  return *reinterpret_cast<float*>(&Result);
}

}  // namespace xe
