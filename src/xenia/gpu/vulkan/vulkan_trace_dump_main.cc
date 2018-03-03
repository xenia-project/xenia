/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/base/main.h"
#include "xenia/gpu/trace_dump.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"

namespace xe {
namespace gpu {
namespace vulkan {

using namespace xe::gpu::xenos;

class VulkanTraceDump : public TraceDump {
 public:
  std::unique_ptr<gpu::GraphicsSystem> CreateGraphicsSystem() override {
    return std::unique_ptr<gpu::GraphicsSystem>(new VulkanGraphicsSystem());
  }
};

int trace_dump_main(const std::vector<std::wstring>& args) {
  VulkanTraceDump trace_dump;
  return trace_dump.Main(args);
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia-gpu-vulkan-trace-dump",
                   L"xenia-gpu-vulkan-trace-dump some.trace",
                   xe::gpu::vulkan::trace_dump_main);
