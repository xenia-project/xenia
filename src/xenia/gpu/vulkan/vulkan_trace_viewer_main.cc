/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <memory>
#include <string>

#include "xenia/base/logging.h"
#include "xenia/gpu/trace_viewer.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/gpu/vulkan/vulkan_graphics_system.h"

namespace xe {
namespace gpu {
namespace vulkan {

using namespace xe::gpu::xenos;

class VulkanTraceViewer final : public TraceViewer {
 public:
  static std::unique_ptr<WindowedApp> Create(
      xe::ui::WindowedAppContext& app_context) {
    return std::unique_ptr<WindowedApp>(new VulkanTraceViewer(app_context));
  }

  std::unique_ptr<gpu::GraphicsSystem> CreateGraphicsSystem() override {
    return std::unique_ptr<gpu::GraphicsSystem>(new VulkanGraphicsSystem());
  }

  uintptr_t GetColorRenderTarget(
      uint32_t pitch, xenos::MsaaSamples samples, uint32_t base,
      xenos::ColorRenderTargetFormat format) override {
    auto command_processor = static_cast<VulkanCommandProcessor*>(
        graphics_system()->command_processor());
    // return command_processor->GetColorRenderTarget(pitch, samples, base,
    //                                               format);
    return 0;
  }

  uintptr_t GetDepthRenderTarget(
      uint32_t pitch, xenos::MsaaSamples samples, uint32_t base,
      xenos::DepthRenderTargetFormat format) override {
    auto command_processor = static_cast<VulkanCommandProcessor*>(
        graphics_system()->command_processor());
    // return command_processor->GetDepthRenderTarget(pitch, samples, base,
    //                                               format);
    return 0;
  }

  uintptr_t GetTextureEntry(const TextureInfo& texture_info,
                            const SamplerInfo& sampler_info) override {
    auto command_processor = static_cast<VulkanCommandProcessor*>(
        graphics_system()->command_processor());

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

 private:
  explicit VulkanTraceViewer(xe::ui::WindowedAppContext& app_context)
      : TraceViewer(app_context, "xenia-gpu-vulkan-trace-viewer") {}
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

XE_DEFINE_WINDOWED_APP(xenia_gpu_vulkan_trace_viewer,
                       xe::gpu::vulkan::VulkanTraceViewer::Create);
