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
#include "xenia/gpu/d3d12/d3d12_command_processor.h"
#include "xenia/gpu/d3d12/d3d12_graphics_system.h"
#include "xenia/gpu/trace_dump.h"
#include "xenia/ui/d3d12/d3d12_provider.h"

namespace xe {
namespace gpu {
namespace d3d12 {

using namespace xe::gpu::xenos;

class D3D12TraceDump : public TraceDump {
 public:
  std::unique_ptr<gpu::GraphicsSystem> CreateGraphicsSystem() override {
    return std::unique_ptr<gpu::GraphicsSystem>(new D3D12GraphicsSystem());
  }

  void BeginHostCapture() override {
    auto provider = static_cast<const ui::d3d12::D3D12Provider*>(
        graphics_system_->provider());
    IDXGraphicsAnalysis* graphics_analysis = provider->GetGraphicsAnalysis();
    if (graphics_analysis) {
      graphics_analysis->BeginCapture();
    }
  }

  void EndHostCapture() override {
    auto provider = static_cast<const ui::d3d12::D3D12Provider*>(
        graphics_system_->provider());
    IDXGraphicsAnalysis* graphics_analysis = provider->GetGraphicsAnalysis();
    if (graphics_analysis) {
      graphics_analysis->EndCapture();
    }
  }
};

int trace_dump_main(const std::vector<std::string>& args) {
  D3D12TraceDump trace_dump;
  return trace_dump.Main(args);
}

}  // namespace d3d12
}  // namespace gpu
}  // namespace xe

XE_DEFINE_CONSOLE_APP("xenia-gpu-d3d12-trace-dump",
                      xe::gpu::d3d12::trace_dump_main, "some.trace",
                      "target_trace_file");
