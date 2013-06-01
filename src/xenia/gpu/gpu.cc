/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/gpu.h>
#include <xenia/gpu/gpu-private.h>

#if XE_PLATFORM(WIN32)
//#include <xenia/gpu/d3d11/d3d11.h>
#endif  // WIN32
#include <xenia/gpu/nop/nop.h>


using namespace xe;
using namespace xe::gpu;


// DEFINE_ flags here.


GraphicsSystem* xe::gpu::Create(const CreationParams* params) {
  // TODO(benvanik): use flags to determine system, check support, etc.
  return xe::gpu::nop::Create(params);
// #if XE_PLATFORM(WIN32)
//   return xe::gpu::d3d11::Create(params);
// #endif  // WIN32
}

GraphicsSystem* xe::gpu::CreateNop(const CreationParams* params) {
  return xe::gpu::nop::Create(params);
}
