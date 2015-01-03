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

// TODO(benvanik): based on platform.
#include <xenia/gpu/gl4/gl4_gpu.h>

DEFINE_string(gpu, "any", "Graphics system. Use: [any, gl4]");

DEFINE_bool(trace_ring_buffer, false, "Trace GPU ring buffer packets.");
DEFINE_string(dump_shaders, "",
              "Path to write GPU shaders to as they are compiled.");

DEFINE_bool(vsync, true, "Enable VSYNC.");

namespace xe {
namespace gpu {

std::unique_ptr<GraphicsSystem> Create(Emulator* emulator) {
  if (FLAGS_gpu.compare("gl4") == 0) {
    return xe::gpu::gl4::Create(emulator);
  } else {
    // Create best available.
    std::unique_ptr<GraphicsSystem> best;

    best = xe::gpu::gl4::Create(emulator);
    if (best) {
      return best;
    }

    // Nothing!
    return nullptr;
  }
}

}  // namespace gpu
}  // namespace xe
