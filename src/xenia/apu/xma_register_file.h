/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_XMA_REGISTER_FILE_H_
#define XENIA_APU_XMA_REGISTER_FILE_H_

#include <cstdint>
#include <cstdlib>

namespace xe {
namespace apu {

enum XmaRegister {
#define XE_XMA_REGISTER(index, type, name) XE_XMA_REG_##name = index,
#include "xenia/apu/xma_register_table.inc"
#undef XE_XMA_REGISTER
};

struct XmaRegisterInfo {
  enum class Type {
    kDword,
    kFloat,
  };
  Type type;
  const char* name;
};

class XmaRegisterFile {
 public:
  XmaRegisterFile();

  static const XmaRegisterInfo* GetRegisterInfo(uint32_t index);

  static const size_t kRegisterCount = (0xFFFF + 1) / 4;
  union RegisterValue {
    uint32_t u32;
    float f32;
  };
  RegisterValue values[kRegisterCount];

  RegisterValue& operator[](int reg) { return values[reg]; }
  RegisterValue& operator[](XmaRegister reg) { return values[reg]; }
};

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XMA_REGISTER_FILE_H_
