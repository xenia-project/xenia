/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/gl4/gl4_gpu.h>

#include <xenia/gpu/gl4/gl4_gpu-private.h>
#include <xenia/gpu/gl4/gl4_graphics_system.h>

DEFINE_bool(thread_safe_gl, false,
            "Only allow one GL context to be active at a time.");

DEFINE_bool(gl_debug_output, false, "Dump ARB_debug_output to stderr.");
DEFINE_bool(gl_debug_output_synchronous, true,
            "ARB_debug_output will synchronize to be thread safe.");

DEFINE_bool(disable_textures, false, "Disable textures and use colors only.");

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

std::unique_ptr<GraphicsSystem> Create(Emulator* emulator) {
  InitializeIfNeeded();
  return std::make_unique<GL4GraphicsSystem>(emulator);
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe
