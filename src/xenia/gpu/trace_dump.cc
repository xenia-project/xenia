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
  if (!FLAGS_target_trace_file.empty()) {
    // Passed as a named argument.
    // TODO(benvanik): find something better than gflags that supports
    // unicode.
    path = xe::to_wstring(FLAGS_target_trace_file);
  } else if (args.size() >= 2) {
    // Passed as an unnamed argument.
    path = args[1];
  }

  // If no path passed, ask the user.
  if (path.empty()) {
    auto file_picker = xe::ui::FilePicker::Create();
    file_picker->set_mode(ui::FilePicker::Mode::kOpen);
    file_picker->set_type(ui::FilePicker::Type::kFile);
    file_picker->set_multi_selection(false);
    file_picker->set_title(L"Select Trace File");
    file_picker->set_extensions({
        {L"Supported Files", L"*.*"}, {L"All Files (*.*)", L"*.*"},
    });
    if (file_picker->Show()) {
      auto selected_files = file_picker->selected_files();
      if (!selected_files.empty()) {
        path = selected_files[0];
      }
    }
  }

  if (path.empty()) {
    xe::FatalError("No trace file specified");
    return 1;
  }

  // Normalize the path and make absolute.
  auto abs_path = xe::to_absolute_path(path);
  XELOGI("Loading trace file %ls...", abs_path.c_str());

  if (!Setup()) {
    xe::FatalError("Unable to setup trace dump tool");
    return 1;
  }
  if (!Load(std::move(abs_path))) {
    xe::FatalError("Unable to load trace file; not found?");
    return 1;
  }

  // Root file name for outputs.
  base_output_path_ =
      xe::fix_path_separators(xe::to_wstring(FLAGS_trace_dump_path));
  base_output_path_ =
      xe::join_paths(base_output_path_,
                     xe::find_name_from_path(xe::fix_path_separators(path)));

  // Ensure output path exists.
  xe::filesystem::CreateParentFolder(base_output_path_);

  Run();
  return 0;
}

bool TraceDump::Setup() {
  // Main display window.
  loop_ = ui::Loop::Create();
  window_ = xe::ui::Window::Create(loop_.get(), L"xenia-gpu-trace-dump");
  loop_->PostSynchronous([&]() {
    xe::threading::set_name("Win32 Loop");
    if (!window_->Initialize()) {
      xe::FatalError("Failed to initialize main window");
      return;
    }
  });
  window_->on_closed.AddListener([&](xe::ui::UIEvent* e) {
    loop_->Quit();
    XELOGI("User-initiated death!");
    exit(1);
  });
  loop_->on_quit.AddListener([&](xe::ui::UIEvent* e) { window_.reset(); });
  window_->Resize(1920, 1200);

  // Create the emulator but don't initialize so we can setup the window.
  emulator_ = std::make_unique<Emulator>(L"");
  X_STATUS result =
      emulator_->Setup(window_.get(), nullptr,
                       [this]() { return CreateGraphicsSystem(); }, nullptr);
  if (XFAILED(result)) {
    XELOGE("Failed to setup emulator: %.8X", result);
    return false;
  }
  memory_ = emulator_->memory();
  graphics_system_ = emulator_->graphics_system();

  window_->on_key_char.AddListener([&](xe::ui::KeyEvent* e) {
    if (e->key_code() == 0x74 /* VK_F5 */) {
      graphics_system_->ClearCaches();
      e->set_handled(true);
    }
  });

  player_ = std::make_unique<TracePlayer>(loop_.get(), graphics_system_);

  window_->on_painting.AddListener([&](xe::ui::UIEvent* e) {
    // Update titlebar status.
    auto file_name = xe::find_name_from_path(trace_file_path_);
    std::wstring title = std::wstring(L"Xenia GPU Trace Dump: ") + file_name;
    if (player_->is_playing_trace()) {
      int percent =
          static_cast<int>(player_->playback_percent() / 10000.0 * 100.0);
      title += xe::format_string(L" (%d%%)", percent);
    }
    window_->set_title(title);

    // Continuous paint.
    window_->Invalidate();
  });
  window_->Invalidate();

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

void TraceDump::Run() {
  loop_->Post([&]() {
    player_->SeekFrame(0);
    player_->SeekCommand(
        static_cast<int>(player_->current_frame()->commands.size() - 1));
  });
  player_->WaitOnPlayback();

  loop_->PostSynchronous([&]() {
    // Breathe.
  });
  loop_->PostSynchronous([&]() {
    // Breathe.
  });

  xe::threading::Fence capture_fence;
  loop_->PostDelayed(
      [&]() {
        // Capture.
        auto raw_image = window_->context()->Capture();

        // Save framebuffer png.
        std::string png_path = xe::to_string(base_output_path_ + L".png");
        stbi_write_png(png_path.c_str(), static_cast<int>(raw_image->width),
                       static_cast<int>(raw_image->height), 4,
                       raw_image->data.data(),
                       static_cast<int>(raw_image->stride));

        capture_fence.Signal();
      },
      50);

  capture_fence.Wait();

  // Wait until we are exited.
  loop_->Quit();
  loop_->AwaitQuit();

  player_.reset();
  emulator_.reset();
  window_.reset();
  loop_.reset();
}

}  //  namespace gpu
}  //  namespace xe
