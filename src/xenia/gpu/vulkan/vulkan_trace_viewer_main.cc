/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/base/main.h"
#include "xenia/gpu/trace_viewer.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"

namespace xe {
namespace gpu {
namespace vulkan {

using namespace xe::gpu::xenos;

class VulkanTraceViewer : public TraceViewer {
 public:
  std::unique_ptr<gpu::GraphicsSystem> CreateGraphicsSystem() override {
    return std::unique_ptr<gpu::GraphicsSystem>(new VulkanGraphicsSystem());
  }

  uintptr_t GetColorRenderTarget(uint32_t pitch, MsaaSamples samples,
                                 uint32_t base,
                                 ColorRenderTargetFormat format) override {
    auto command_processor = static_cast<VulkanCommandProcessor*>(
        graphics_system_->command_processor());
    // return command_processor->GetColorRenderTarget(pitch, samples, base,
    //                                               format);
    return 0;
  }

  uintptr_t GetDepthRenderTarget(uint32_t pitch, MsaaSamples samples,
                                 uint32_t base,
                                 DepthRenderTargetFormat format) override {
    auto command_processor = static_cast<VulkanCommandProcessor*>(
        graphics_system_->command_processor());
    // return command_processor->GetDepthRenderTarget(pitch, samples, base,
    //                                               format);
    return 0;
  }

  uintptr_t GetTextureEntry(const TextureInfo& texture_info,
                            const SamplerInfo& sampler_info) override {
    auto command_processor = static_cast<VulkanCommandProcessor*>(
        graphics_system_->command_processor());

    // auto entry_view =
    //    command_processor->texture_cache()->Demand(texture_info,
    //    sampler_info);
    // if (!entry_view) {
    //  return 0;
    //}
    // auto texture = entry_view->texture;
    // return static_cast<uintptr_t>(texture->handle);
    return 0;
  }
};

int trace_viewer_main(const std::vector<std::wstring>& args) {
  VulkanTraceViewer trace_viewer;
  return trace_viewer.Main(args);
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia-gpu-vulkan-trace-viewer",
                   xe::gpu::vulkan::trace_viewer_main, "some.trace",
                   "target_trace_file");
