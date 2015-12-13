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
#include "xenia/gpu/trace_viewer.h"

namespace xe {
namespace gpu {
namespace gl4 {

using namespace xe::gpu::xenos;

class GL4TraceViewer : public TraceViewer {
 public:
  std::unique_ptr<gpu::GraphicsSystem> CreateGraphicsSystem() override {
    return std::unique_ptr<gpu::GraphicsSystem>(new GL4GraphicsSystem());
  }

  uintptr_t GetColorRenderTarget(uint32_t pitch, MsaaSamples samples,
                                 uint32_t base,
                                 ColorRenderTargetFormat format) override {
    auto command_processor = static_cast<GL4CommandProcessor*>(
        graphics_system_->command_processor());
    return command_processor->GetColorRenderTarget(pitch, samples, base,
                                                   format);
  }

  uintptr_t GetDepthRenderTarget(uint32_t pitch, MsaaSamples samples,
                                 uint32_t base,
                                 DepthRenderTargetFormat format) override {
    auto command_processor = static_cast<GL4CommandProcessor*>(
        graphics_system_->command_processor());
    return command_processor->GetDepthRenderTarget(pitch, samples, base,
                                                   format);
  }

  uintptr_t GetTextureEntry(const TextureInfo& texture_info,
                            const SamplerInfo& sampler_info) override {
    auto command_processor = static_cast<GL4CommandProcessor*>(
        graphics_system_->command_processor());

    auto entry_view =
        command_processor->texture_cache()->Demand(texture_info, sampler_info);
    if (!entry_view) {
      return 0;
    }
    auto texture = entry_view->texture;
    return static_cast<uintptr_t>(texture->handle);
  }
};

int trace_viewer_main(const std::vector<std::wstring>& args) {
  GL4TraceViewer trace_viewer;
  return trace_viewer.Main(args);
}

}  // namespace gl4
}  // namespace gpu
}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia-gpu-gl4-trace-viewer",
                   L"xenia-gpu-gl4-trace-viewer some.trace",
                   xe::gpu::gl4::trace_viewer_main);
