/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_XENOS_REGISTERS_H_
#define XENIA_GPU_XENOS_REGISTERS_H_

#include <xenia/core.h>


namespace xe {
namespace gpu {
namespace xenos {


static const uint32_t kXEGpuRegisterCount = 0x5003;


enum Registers {
#define XE_GPU_REGISTER(index, type, name) \
    XE_GPU_REG_##name = index,
#include <xenia/gpu/xenos/register_table.inc>
#undef XE_GPU_REGISTER
};


const char* GetRegisterName(uint32_t index);


union RegisterValue {
  uint32_t  u32;
  float     f32;
};


struct RegisterFile {
  RegisterValue   values[kXEGpuRegisterCount];
};


}  // namespace xenos
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_XENOS_REGISTERS_H_
