/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/xma_register_file.h"

#include <cstring>

#include "xenia/base/math.h"

namespace xe {
namespace apu {

XmaRegisterFile::XmaRegisterFile() { std::memset(values, 0, sizeof(values)); }

const XmaRegisterInfo* XmaRegisterFile::GetRegisterInfo(uint32_t index) {
  switch (index) {
#define XE_XMA_REGISTER(index, type, name)    \
  case index: {                               \
    static const XmaRegisterInfo reg_info = { \
        XmaRegisterInfo::Type::type,          \
        #name,                                \
    };                                        \
    return &reg_info;                         \
  }
#include "xenia/apu/xma_register_table.inc"
#undef XE_XMA_REGISTER
    default:
      return nullptr;
  }
}

}  //  namespace apu
}  //  namespace xe
