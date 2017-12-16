/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/base/main.h"
#include "xenia/gpu/gl4/gl4_command_processor.h"
#include "xenia/gpu/gl4/gl4_graphics_system.h"
#include "xenia/gpu/trace_dump.h"

namespace xe {
namespace gpu {
namespace gl4 {

using namespace xe::gpu::xenos;

class GL4TraceDump : public TraceDump {
 public:
  std::unique_ptr<gpu::GraphicsSystem> CreateGraphicsSystem() override {
    return std::unique_ptr<gpu::GraphicsSystem>(new GL4GraphicsSystem());
  }
};

int trace_dump_main(const std::vector<std::wstring>& args) {
  GL4TraceDump trace_dump;
  return trace_dump.Main(args);
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia-gpu-gl4-trace-dump",
                   L"xenia-gpu-gl4-trace-dump some.trace",
                   xe::gpu::gl4::trace_dump_main);
