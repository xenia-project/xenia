/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/xenos/registers.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


const char* xe::gpu::xenos::GetRegisterName(uint32_t index) {
  switch (index) {
#define XE_GPU_REGISTER(index, type, name) \
    case index: return #name;
#include <xenia/gpu/xenos/register_table.inc>
#undef XE_GPU_REGISTER
    default:
      return NULL;
  }
}
