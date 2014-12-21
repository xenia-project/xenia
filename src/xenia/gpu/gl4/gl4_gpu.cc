/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/gl4/gl4_gpu.h>

//#include <xenia/gpu/gl4/gl4_graphics_system.h>

namespace xe {
namespace gpu {
namespace gl4 {

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

class GL4GraphicsSystem : public GraphicsSystem {
 public:
  GL4GraphicsSystem(Emulator* emulator) : GraphicsSystem(emulator) {}
  ~GL4GraphicsSystem() override = default;
};

std::unique_ptr<GraphicsSystem> Create(Emulator* emulator) {
  InitializeIfNeeded();
  return std::make_unique<GL4GraphicsSystem>(emulator);
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
