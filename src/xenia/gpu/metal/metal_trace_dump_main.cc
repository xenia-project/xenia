/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <chrono>
#include <cstdlib>
#include <thread>

#include "third_party/metal-cpp/Metal/Metal.hpp"
#include "third_party/stb/stb_image_write.h"
#include "xenia/base/console_app_main.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"
#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/gpu/metal/metal_graphics_system.h"
#include "xenia/gpu/trace_dump.h"
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
    // Check if GPU capture is enabled via environment variable
    const char* capture_enabled = std::getenv("XENIA_GPU_CAPTURE_ENABLED");
    if (!capture_enabled || std::string(capture_enabled) != "1") {
      XELOGI(
          "Metal GPU capture disabled (set XENIA_GPU_CAPTURE_ENABLED=1 to "
          "enable)");
      return;
    }

    // Get capture output directory from environment
    const char* capture_dir = std::getenv("XENIA_GPU_CAPTURE_DIR");
    if (!capture_dir) {
      capture_dir = ".";
    }

    // Get the command queue from the command processor
    if (!metal_graphics_system_) {
      XELOGW("MetalTraceDump: No graphics system for GPU capture");
      return;
    }

    auto* cmd_proc = static_cast<MetalCommandProcessor*>(
        metal_graphics_system_->command_processor());
    if (!cmd_proc) {
      XELOGW("MetalTraceDump: No command processor for GPU capture");
      return;
    }

    MTL::CommandQueue* command_queue = cmd_proc->GetMetalCommandQueue();
    if (!command_queue) {
      XELOGW("MetalTraceDump: No command queue for GPU capture");
      return;
    }

    // Start programmatic GPU capture
    capture_manager_ = MTL::CaptureManager::sharedCaptureManager();
    if (!capture_manager_) {
      XELOGW("MetalTraceDump: GPU capture manager not available");
      return;
    }

    auto* descriptor = MTL::CaptureDescriptor::alloc()->init();
    descriptor->setCaptureObject(command_queue);
    descriptor->setDestination(MTL::CaptureDestinationGPUTraceDocument);

    // Create output path
    std::string capture_path =
        std::string(capture_dir) + "/gpu_capture.gputrace";
    auto* url = NS::URL::fileURLWithPath(
        NS::String::string(capture_path.c_str(), NS::UTF8StringEncoding));
    descriptor->setOutputURL(url);

    NS::Error* error = nullptr;
    if (capture_manager_->startCapture(descriptor, &error)) {
      XELOGI("MetalTraceDump: Started GPU capture to {}", capture_path);
      is_capturing_ = true;
    } else {
      XELOGE("MetalTraceDump: Failed to start GPU capture: {}",
             error ? error->localizedDescription()->utf8String() : "unknown");
    }

    descriptor->release();
  }

  void EndHostCapture() override {
    // Ensure the final frame is pushed to the presenter even if the trace
    // didn't contain a swap command.
    if (metal_graphics_system_) {
      auto* cmd_proc = static_cast<MetalCommandProcessor*>(
          metal_graphics_system_->command_processor());
      if (cmd_proc) {
        XELOGI("MetalTraceDump: Forcing swap to ensure frame capture...");
        cmd_proc->ForceIssueSwap();
      }
    }

    // Stop GPU capture if we started one
    if (capture_manager_ && is_capturing_) {
      capture_manager_->stopCapture();
      XELOGI("MetalTraceDump: GPU capture completed");
      is_capturing_ = false;
    }
  }

 private:
  MetalGraphicsSystem* metal_graphics_system_ = nullptr;
  std::unique_ptr<MetalTraceDumpPresenter> dummy_presenter_;
  MTL::CaptureManager* capture_manager_ = nullptr;
  bool is_capturing_ = false;

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
