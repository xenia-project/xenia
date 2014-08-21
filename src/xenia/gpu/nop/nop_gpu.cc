/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/nop/nop_gpu.h>

#include <xenia/gpu/nop/nop_graphics_system.h>

using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::nop;

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

void CleanupOnShutdown() {}
}

std::unique_ptr<GraphicsSystem> xe::gpu::nop::Create(Emulator* emulator) {
  InitializeIfNeeded();
  return std::make_unique<NopGraphicsSystem>(emulator);
}
