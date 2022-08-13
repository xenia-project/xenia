/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_REGISTER_FILE_H_
#define XENIA_GPU_REGISTER_FILE_H_

#include <cstdint>
#include <cstdlib>

#include "xenia/gpu/registers.h"

namespace xe {
namespace gpu {

struct RegisterInfo {
  enum class Type {
    kDword,
    kFloat,
  };
  Type type;
  const char* name;
};

class RegisterFile {
 public:
  RegisterFile();

  static const RegisterInfo* GetRegisterInfo(uint32_t index);
  static bool IsValidRegister(uint32_t index);
  static constexpr size_t kRegisterCount = 0x5003;
  union RegisterValue {
    uint32_t u32;
    float f32;
  };
  RegisterValue values[kRegisterCount];

  const RegisterValue& operator[](uint32_t reg) const { return values[reg]; }
  RegisterValue& operator[](uint32_t reg) { return values[reg]; }
  const RegisterValue& operator[](Register reg) const { return values[reg]; }
  RegisterValue& operator[](Register reg) { return values[reg]; }
  template <typename T>
  const T& Get(uint32_t reg) const {
    return *reinterpret_cast<const T*>(&values[reg]);
  }
  template <typename T>
  T& Get(uint32_t reg) {
    return *reinterpret_cast<T*>(&values[reg]);
  }
  template <typename T>
  const T& Get(Register reg) const {
    return *reinterpret_cast<const T*>(&values[reg]);
  }
  template <typename T>
  T& Get(Register reg) {
    return *reinterpret_cast<T*>(&values[reg]);
  }
  template <typename T>
  const T& Get() const {
    return *reinterpret_cast<const T*>(&values[T::register_index]);
  }
  template <typename T>
  T& Get() {
    return *reinterpret_cast<T*>(&values[T::register_index]);
  }
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_REGISTER_FILE_H_
