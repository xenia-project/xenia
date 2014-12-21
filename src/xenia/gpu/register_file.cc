/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/register_file.h>

namespace xe {
namespace gpu {

RegisterFile::RegisterFile() { memset(values, 0, sizeof(values)); }

const char* RegisterFile::GetRegisterName(uint32_t index) {
  switch (index) {
#define XE_GPU_REGISTER(index, type, name) \
  case index:                              \
    return #name;
#include <xenia/gpu/register_table.inc>
#undef XE_GPU_REGISTER
    default:
      return nullptr;
  }
}

}  //  namespace gpu
}  //  namespace xe
