/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <chrono>
#include <thread>

#include "xenia/base/console_app_main.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/gpu/metal/metal_graphics_system.h"
#include "xenia/gpu/trace_dump.h"
// #include "xenia/gpu/metal/metal_render_target_cache.h"
#include "third_party/stb/stb_image_write.h"
#include "xenia/ui/metal/metal_presenter.h"
#include "xenia/ui/presenter.h"

namespace xe {
namespace gpu {
namespace metal {

using namespace xe::gpu::xenos;

// Dummy presenter for trace dumps that captures from command processor
class MetalTraceDumpPresenter : public ui::Presenter {
 public:
  MetalTraceDumpPresenter(MetalGraphicsSystem* graphics_system)
      : Presenter(FatalErrorHostGpuLossCallback),
        graphics_system_(graphics_system) {}

  ui::Surface::TypeFlags GetSupportedSurfaceTypes() const override {
    return ui::Surface::TypeFlags(0);  // No surface needed for trace dump
  }

  bool CaptureGuestOutput(ui::RawImage& raw_image) override {
    XELOGI("MetalTraceDumpPresenter::CaptureGuestOutput called");
    // Use the graphics system's implementation which handles command processor
    //    return graphics_system_->CaptureGuestOutput(raw_image);
    return false;
  }

 protected:
  // Required virtual methods - all no-ops for trace dump
  SurfacePaintConnectResult ConnectOrReconnectPaintingToSurfaceFromUIThread(
      ui::Surface& new_surface, uint32_t new_surface_width,
      uint32_t new_surface_height, bool was_paintable,
      bool& is_vsync_implicit_out) override {
    is_vsync_implicit_out = false;
    return SurfacePaintConnectResult::kSuccess;  // Always succeed
  }

  void DisconnectPaintingFromSurfaceFromUIThreadImpl() override {
    // No-op for trace dump
  }

  bool RefreshGuestOutputImpl(
      uint32_t mailbox_index, uint32_t frontbuffer_width,
      uint32_t frontbuffer_height,
      std::function<bool(GuestOutputRefreshContext& context)> refresher,
      bool& is_8bpc_out_ref) override {
    // No-op for trace dump - we capture directly
    is_8bpc_out_ref = false;
    return true;  // Always report success
  }

  PaintResult PaintAndPresentImpl(bool execute_ui_drawers) override {
    return PaintResult::kPresented;  // Always report success
  }

 private:
  MetalGraphicsSystem* graphics_system_;
};

class MetalTraceDump : public TraceDump {
 public:
  std::unique_ptr<gpu::GraphicsSystem> CreateGraphicsSystem() override {
    auto graphics_system = std::make_unique<MetalGraphicsSystem>();
    metal_graphics_system_ = graphics_system.get();
    return graphics_system;
  }

  void BeginHostCapture() override {
    // Metal frame capture can be triggered through Xcode's Metal debugger
    // or through Metal Performance Shaders frame capture API
    // For now, we'll add a log message indicating capture should be started
    // manually through Xcode GPU debugging tools
    //    XELOGI("Metal frame capture: Start capture manually through Xcode GPU
    //    debugging tools");
    //
    //    // Install dummy presenter for trace dump capture
    //    XELOGI("MetalTraceDump: Checking presenter, current={}",
    //           graphics_system_->presenter() ? "yes" : "no");
    //    if (!graphics_system_->presenter()) {
    //      dummy_presenter_ =
    //      std::make_unique<MetalTraceDumpPresenter>(metal_graphics_system_);
    //      metal_graphics_system_->SetPresenterForTesting(dummy_presenter_.get());
    //      XELOGI("MetalTraceDump: Dummy presenter installed, now={}",
    //             metal_graphics_system_->presenter() ? "yes" : "no");
    //    }
  }

  void EndHostCapture() override {
    // Metal frame capture end - capture should be controlled via Xcode’s
    // GPU debugging tools, consistent with how D3D12 and Vulkan trace dumps
    // only use BeginHostCapture / EndHostCapture for external tooling.
    XELOGI(
        "Metal frame capture: End capture manually through Xcode GPU debugging "
        "tools");
  }

 private:
  MetalGraphicsSystem* metal_graphics_system_ = nullptr;
  std::unique_ptr<MetalTraceDumpPresenter> dummy_presenter_;

 public:
  int Main(const std::vector<std::string>& args) {
    // Store args for PNG path generation since base class members are private
    png_output_path_ = "trace_output.png";  // Default
    if (args.size() >= 2) {
      png_output_path_ =
          std::filesystem::path(args[1]).replace_extension(".png");
    }

    // Use base implementation to set up, but our overridden Run() method
    return TraceDump::Main(args);
  }

 private:
  std::filesystem::path png_output_path_;
};

int trace_dump_main(const std::vector<std::string>& args) {
  MetalTraceDump trace_dump;
  return trace_dump.Main(args);
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe

XE_DEFINE_CONSOLE_APP("xenia-gpu-metal-trace-dump",
                      xe::gpu::metal::trace_dump_main, "some.trace",
                      "target_trace_file");
