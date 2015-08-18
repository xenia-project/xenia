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

namespace xe {
namespace gpu {

enum Register {
#define XE_GPU_REGISTER(index, type, name) XE_GPU_REG_##name = index,
#include "xenia/gpu/register_table.inc"
#undef XE_GPU_REGISTER
};

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

  static const size_t kRegisterCount = 0x5003;
  union RegisterValue {
    uint32_t u32;
    float f32;
  };
  RegisterValue values[kRegisterCount];

  RegisterValue& operator[](int reg) { return values[reg]; }
  RegisterValue& operator[](Register reg) { return values[reg]; }
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_REGISTER_FILE_H_
