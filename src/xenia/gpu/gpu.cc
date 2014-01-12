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


DEFINE_bool(trace_ring_buffer, false,
    "Trace GPU ring buffer packets.");
DEFINE_string(dump_shaders, "",
    "Path to write GPU shaders to as they are compiled.");


#include <xenia/gpu/nop/nop_gpu.h>
GraphicsSystem* xe::gpu::CreateNop(Emulator* emulator) {
  return xe::gpu::nop::Create(emulator);
}


#if XE_PLATFORM_WIN32
#include <xenia/gpu/d3d11/d3d11_gpu.h>
GraphicsSystem* xe::gpu::CreateD3D11(Emulator* emulator) {
  return xe::gpu::d3d11::Create(emulator);
}
#endif  // WIN32


GraphicsSystem* xe::gpu::Create(Emulator* emulator) {
  if (FLAGS_gpu.compare("nop") == 0) {
    return CreateNop(emulator);
#if XE_PLATFORM_WIN32
  } else if (FLAGS_gpu.compare("d3d11") == 0) {
    return CreateD3D11(emulator);
#endif  // WIN32
  } else {
    // Create best available.
    GraphicsSystem* best = NULL;

#if XE_PLATFORM_WIN32
    best = CreateD3D11(emulator);
    if (best) {
      return best;
    }
#endif  // WIN32

    // Fallback to nop.
    return CreateNop(emulator);
  }
}
