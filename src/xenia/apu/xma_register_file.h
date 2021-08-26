/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_XMA_REGISTER_FILE_H_
#define XENIA_APU_XMA_REGISTER_FILE_H_

#include <cstdint>
#include <cstdlib>

namespace xe {
namespace apu {

struct XmaRegister {
#define XE_XMA_REGISTER(index, name) static const uint32_t name = index;
#include "xenia/apu/xma_register_table.inc"
#undef XE_XMA_REGISTER
};

struct XmaRegisterInfo {
  const char* name;
};

class XmaRegisterFile {
 public:
  XmaRegisterFile();

  static const XmaRegisterInfo* GetRegisterInfo(uint32_t index);

  static const size_t kRegisterCount = (0xFFFF + 1) / 4;
  uint32_t values[kRegisterCount];

  uint32_t operator[](uint32_t reg) const { return values[reg]; }
  uint32_t& operator[](uint32_t reg) { return values[reg]; }
};

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_XMA_REGISTER_FILE_H_
