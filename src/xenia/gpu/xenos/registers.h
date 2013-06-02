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


union RegisterValue {
  uint32_t  uint_value;
  float     float_value;
};


struct RegisterFile {
  // TODO(benvanik): figure out the actual number.
  RegisterValue   registers[0xFFFF];
};


}  // namespace xenos
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_XENOS_REGISTERS_H_
