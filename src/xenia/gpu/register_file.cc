/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/register_file.h"

#include <cstring>

#include "xenia/base/math.h"

namespace xe {
namespace gpu {

RegisterFile::RegisterFile() { std::memset(values, 0, sizeof(values)); }

const RegisterInfo* RegisterFile::GetRegisterInfo(uint32_t index) {
  switch (index) {
#define XE_GPU_REGISTER(index, type, name) \
  case index: {                            \
    static const RegisterInfo reg_info = { \
        RegisterInfo::Type::type,          \
        #name,                             \
    };                                     \
    return &reg_info;                      \
  }
#include "xenia/gpu/register_table.inc"
#undef XE_GPU_REGISTER
    default:
      return nullptr;
  }
}

}  //  namespace gpu
}  //  namespace xe
