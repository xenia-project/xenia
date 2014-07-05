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

#include <xenia/core.h>


namespace xe {
namespace gpu {


enum Register {
#define XE_GPU_REGISTER(index, type, name) \
    XE_GPU_REG_##name = index,
#include <xenia/gpu/xenos/register_table.inc>
#undef XE_GPU_REGISTER
};


class RegisterFile {
public:
  RegisterFile();

  const char* GetRegisterName(uint32_t index);

  static const size_t kRegisterCount = 0x5003;
  union RegisterValue {
    uint32_t  u32;
    float     f32;
  };
  RegisterValue values[kRegisterCount];

  RegisterValue& operator[](Register reg) {
    return values[reg];
  }
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_REGISTER_FILE_H_
