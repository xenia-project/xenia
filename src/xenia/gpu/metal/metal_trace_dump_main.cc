/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/console_app_main.h"
#include "xenia/base/logging.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/string.h"
#include "xenia/gpu/trace_dump.h"
#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/gpu/metal/metal_graphics_system.h"
#include "xenia/gpu/metal/metal_render_target_cache.h"
#include "xenia/ui/metal/metal_presenter.h"
#include "third_party/stb/stb_image_write.h"

namespace xe {
namespace gpu {
namespace metal {

using namespace xe::gpu::xenos;

class MetalTraceDump : public TraceDump {
 public:
  std::unique_ptr<gpu::GraphicsSystem> CreateGraphicsSystem() override {
    return std::unique_ptr<gpu::GraphicsSystem>(new MetalGraphicsSystem());
  }

  void BeginHostCapture() override {
    // Metal frame capture can be triggered through Xcode's Metal debugger
    // or through Metal Performance Shaders frame capture API
    // For now, we'll add a log message indicating capture should be started
    // manually through Xcode GPU debugging tools
    XELOGI("Metal frame capture: Start capture manually through Xcode GPU debugging tools");
  }

  void EndHostCapture() override {
    // Metal frame capture end - log message for manual capture
    XELOGI("Metal frame capture: End capture manually through Xcode GPU debugging tools");
  }

  int Main(const std::vector<std::string>& args) {
    // Use base implementation to set up and run
    int result = TraceDump::Main(args);
    
    // If the base implementation failed to capture (no presenter), try direct Metal capture
    if (result != 0 && graphics_system_) {
      XELOGI("MetalTraceDump: Base capture failed, trying direct Metal capture");
      
      auto* metal_graphics_system = static_cast<MetalGraphicsSystem*>(graphics_system_);
      ui::RawImage raw_image;
      
      if (metal_graphics_system->CaptureGuestOutput(raw_image)) {
        // Reconstruct the output path (same logic as TraceDump::Main)
        std::filesystem::path png_path;
        if (args.size() >= 2) {
          png_path = std::filesystem::path(args[1]).replace_extension(".png");
        } else {
          png_path = "trace_output.png";
        }
        
        // Save PNG
        XELOGI("MetalTraceDump: Saving PNG to {}", xe::path_to_utf8(png_path));
        auto handle = xe::filesystem::OpenFile(png_path, "wb");
        if (handle) {
          auto callback = [](void* context, void* data, int size) {
            fwrite(data, 1, size, (FILE*)context);
          };
          stbi_write_png_to_func(callback, handle,
                                static_cast<int>(raw_image.width),
                                static_cast<int>(raw_image.height), 4,
                                raw_image.data.data(),
                                static_cast<int>(raw_image.stride));
          fclose(handle);
          XELOGI("MetalTraceDump: PNG saved successfully");
          return 0;  // Success
        } else {
          XELOGE("MetalTraceDump: Failed to open PNG file for writing");
        }
      } else {
        XELOGE("MetalTraceDump: Direct Metal capture also failed");
      }
    }
    
    return result;
  }
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
