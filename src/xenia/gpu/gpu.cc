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



using namespace xe;
using namespace xe::gpu;


DEFINE_string(gpu, "any",
    "Graphics system. Use: [any, nop, d3d11]");


#include <xenia/gpu/nop/nop.h>
GraphicsSystem* xe::gpu::CreateNop(const CreationParams* params) {
  return xe::gpu::nop::Create(params);
}


#if XE_PLATFORM(WIN32)
#include <xenia/gpu/d3d11/d3d11.h>
GraphicsSystem* xe::gpu::CreateD3D11(const CreationParams* params) {
  return xe::gpu::d3d11::Create(params);
}
#endif  // WIN32


GraphicsSystem* xe::gpu::Create(const CreationParams* params) {
  if (FLAGS_gpu.compare("nop") == 0) {
    return CreateNop(params);
#if XE_PLATFORM(WIN32)
  } else if (FLAGS_gpu.compare("d3d11") == 0) {
    return CreateD3D11(params);
#endif  // WIN32
  } else {
    // Create best available.
    GraphicsSystem* best = NULL;

#if XE_PLATFORM(WIN32)
    best = CreateD3D11(params);
    if (best) {
      return best;
    }
#endif  // WIN32

    // Fallback to nop.
    return CreateNop(params);
  }
}
