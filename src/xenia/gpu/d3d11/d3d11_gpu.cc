/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_gpu.h>

#include <xenia/gpu/d3d11/d3d11_graphics_system.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;


namespace {
  void InitializeIfNeeded();
  void CleanupOnShutdown();

  void InitializeIfNeeded() {
    static bool has_initialized = false;
    if (has_initialized) {
      return;
    }
    has_initialized = true;

    //

    atexit(CleanupOnShutdown);
  }

  void CleanupOnShutdown() {
  }
}


GraphicsSystem* xe::gpu::d3d11::Create(Emulator* emulator) {
  InitializeIfNeeded();
  return new D3D11GraphicsSystem(emulator);
}
