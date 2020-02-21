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
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/gpu/d3d12/d3d12_graphics_system.h"
#include "xenia/gpu/trace_viewer.h"

namespace xe {
namespace gpu {
namespace d3d12 {

using namespace xe::gpu::xenos;

class D3D12TraceViewer : public TraceViewer {
 public:
  std::unique_ptr<gpu::GraphicsSystem> CreateGraphicsSystem() override {
    return std::unique_ptr<gpu::GraphicsSystem>(new D3D12GraphicsSystem());
  }

  uintptr_t GetColorRenderTarget(uint32_t pitch, MsaaSamples samples,
                                 uint32_t base,
                                 ColorRenderTargetFormat format) override {
    // TODO(Triang3l): EDRAM viewer.
    return 0;
  }

  uintptr_t GetDepthRenderTarget(uint32_t pitch, MsaaSamples samples,
                                 uint32_t base,
                                 DepthRenderTargetFormat format) override {
    // TODO(Triang3l): EDRAM viewer.
    return 0;
  }

  uintptr_t GetTextureEntry(const TextureInfo& texture_info,
                            const SamplerInfo& sampler_info) override {
    // TODO(Triang3l): Textures, but from a fetch constant rather than
    // TextureInfo/SamplerInfo which are going away.
    return 0;
  }
};

int trace_viewer_main(const std::vector<std::wstring>& args) {
  D3D12TraceViewer trace_viewer;
  return trace_viewer.Main(args);
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia-gpu-d3d12-trace-viewer",
                   xe::gpu::d3d12::trace_viewer_main, "some.trace",
                   "target_trace_file");
