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

public:
  
  int Main(const std::vector<std::string>& args) {
    // Store args for PNG path generation since base class members are private
    png_output_path_ = "trace_output.png";  // Default
    if (args.size() >= 2) {
      png_output_path_ = std::filesystem::path(args[1]).replace_extension(".png");
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
