/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/trace_dump.h"

#include <gflags/gflags.h>

#include "third_party/stb/stb_image_write.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/base/string.h"
#include "xenia/base/threading.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/memory.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/window.h"
#include "xenia/xbox.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#undef _CRT_SECURE_NO_WARNINGS
#undef _CRT_NONSTDC_NO_DEPRECATE
#include "third_party/stb/stb_image_write.h"

DEFINE_string(target_trace_file, "", "Specifies the trace file to load.");
DEFINE_string(trace_dump_path, "", "Output path for dumped files.");

namespace xe {
namespace gpu {

using namespace xe::gpu::xenos;

TraceDump::TraceDump() = default;

TraceDump::~TraceDump() = default;

int TraceDump::Main(const std::vector<std::wstring>& args) {
  // Grab path from the flag or unnamed argument.
  std::wstring path;
  std::wstring output_path;
  if (!FLAGS_target_trace_file.empty()) {
    // Passed as a named argument.
    // TODO(benvanik): find something better than gflags that supports
    // unicode.
    path = xe::to_wstring(FLAGS_target_trace_file);
  } else if (args.size() >= 2) {
    // Passed as an unnamed argument.
    path = args[1];

    if (args.size() >= 3) {
      output_path = args[2];
    }
  }

  if (path.empty()) {
    XELOGE("No trace file specified");
    return 5;
  }

  // Normalize the path and make absolute.
  auto abs_path = xe::to_absolute_path(path);
  XELOGI("Loading trace file %ls...", abs_path.c_str());

  if (!Setup()) {
    XELOGE("Unable to setup trace dump tool");
    return 4;
  }
  if (!Load(std::move(abs_path))) {
    XELOGE("Unable to load trace file; not found?");
    return 5;
  }

  // Root file name for outputs.
  if (output_path.empty()) {
    base_output_path_ =
        xe::fix_path_separators(xe::to_wstring(FLAGS_trace_dump_path));

    std::wstring output_name =
        xe::find_name_from_path(xe::fix_path_separators(path));

    // Strip the extension from the filename.
    auto last_dot = output_name.find_last_of(L".");
    if (last_dot != std::string::npos) {
      output_name = output_name.substr(0, last_dot);
    }

    base_output_path_ = xe::join_paths(base_output_path_, output_name);
  } else {
    base_output_path_ = xe::fix_path_separators(output_path);
  }

  // Ensure output path exists.
  xe::filesystem::CreateParentFolder(base_output_path_);

  return Run();
}

bool TraceDump::Setup() {
  // Create the emulator but don't initialize so we can setup the window.
  emulator_ = std::make_unique<Emulator>(L"");
  X_STATUS result = emulator_->Setup(
      nullptr, nullptr, [this]() { return CreateGraphicsSystem(); }, nullptr);
  if (XFAILED(result)) {
    XELOGE("Failed to setup emulator: %.8X", result);
    return false;
  }
  graphics_system_ = emulator_->graphics_system();
  player_ = std::make_unique<TracePlayer>(nullptr, graphics_system_);
  return true;
}

bool TraceDump::Load(std::wstring trace_file_path) {
  trace_file_path_ = std::move(trace_file_path);

  if (!player_->Open(trace_file_path_)) {
    XELOGE("Could not load trace file");
    return false;
  }

  return true;
}

int TraceDump::Run() {
  player_->SeekFrame(0);
  player_->SeekCommand(
      static_cast<int>(player_->current_frame()->commands.size() - 1));
  player_->WaitOnPlayback();

  // Capture.
  int result = 0;
  auto raw_image = graphics_system_->Capture();
  if (raw_image) {
    // Save framebuffer png.
    std::string png_path = xe::to_string(base_output_path_ + L".png");
    stbi_write_png(png_path.c_str(), static_cast<int>(raw_image->width),
                   static_cast<int>(raw_image->height), 4,
                   raw_image->data.data(), static_cast<int>(raw_image->stride));
  } else {
    result = 1;
  }

  player_.reset();
  emulator_.reset();
  return result;
}

}  //  namespace gpu
}  //  namespace xe
