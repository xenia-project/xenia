/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/console_app_main.h"
#include "xenia/base/logging.h"
#include "xenia/gpu/trace_dump.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace gpu {
namespace vulkan {

using namespace xe::gpu::xenos;

class VulkanTraceDump : public TraceDump {
 public:
  std::unique_ptr<gpu::GraphicsSystem> CreateGraphicsSystem() override {
    return std::unique_ptr<gpu::GraphicsSystem>(new VulkanGraphicsSystem());
  }

  void BeginHostCapture() override {
    auto device = static_cast<const ui::vulkan::VulkanProvider*>(
                      graphics_system_->provider())
                      ->device();
    if (device->is_renderdoc_attached()) {
      device->BeginRenderDocFrameCapture();
    }
  }

  void EndHostCapture() override {
    auto device = static_cast<const ui::vulkan::VulkanProvider*>(
                      graphics_system_->provider())
                      ->device();
    if (device->is_renderdoc_attached()) {
      device->EndRenderDocFrameCapture();
    }
  }
};

int trace_dump_main(const std::vector<std::string>& args) {
  VulkanTraceDump trace_dump;
  return trace_dump.Main(args);
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

XE_DEFINE_CONSOLE_APP("xenia-gpu-vulkan-trace-dump",
                      xe::gpu::vulkan::trace_dump_main, "some.trace",
                      "target_trace_file");
