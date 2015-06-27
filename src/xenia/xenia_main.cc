/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <gflags/gflags.h>

#include "xenia/base/logging.h"
#include "xenia/base/main.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel.h"
#include "xenia/profiling.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/main_window.h"

DEFINE_string(target, "", "Specifies the target .xex or .iso to execute.");

namespace xe {

int xenia_main(std::vector<std::wstring>& args) {
  Profiler::Initialize();
  Profiler::ThreadEnter("main");

  // Create the emulator.
  auto emulator = std::make_unique<Emulator>(L"");
  X_STATUS result = emulator->Setup();
  if (XFAILED(result)) {
    XELOGE("Failed to setup emulator: %.8X", result);
    return 1;
  }

  // Grab path from the flag or unnamed argument.
  std::wstring path;
  if (!FLAGS_target.empty() || args.size() >= 2) {
    if (!FLAGS_target.empty()) {
      // Passed as a named argument.
      // TODO(benvanik): find something better than gflags that supports
      // unicode.
      path = xe::to_wstring(FLAGS_target);
    } else {
      // Passed as an unnamed argument.
      path = args[1];
    }
  }

  // If no path passed, ask the user.
  if (path.empty()) {
    ui::PlatformFilePicker file_picker;
    file_picker.set_mode(ui::FilePicker::Mode::kOpen);
    file_picker.set_type(ui::FilePicker::Type::kFile);
    file_picker.set_multi_selection(false);
    file_picker.set_title(L"Select Content Package");
    file_picker.set_extensions({
        {L"Supported Files", L"*.iso;*.xex;*.xcp;*.*"},
        {L"Disc Image (*.iso)", L"*.iso"},
        {L"Xbox Executable (*.xex)", L"*.xex"},
        //{ L"Content Package (*.xcp)", L"*.xcp" },
        {L"All Files (*.*)", L"*.*"},
    });
    if (file_picker.Show(emulator->main_window()->hwnd())) {
      auto selected_files = file_picker.selected_files();
      if (!selected_files.empty()) {
        path = selected_files[0];
      }
    }
  }

  if (!path.empty()) {
    // Normalize the path and make absolute.
    std::wstring abs_path = xe::to_absolute_path(path);

    result = emulator->LaunchPath(abs_path);
    if (XFAILED(result)) {
      XELOGE("Failed to launch target: %.8X", result);
      return 1;
    }

    // Wait until we are exited.
    emulator->main_window()->loop()->AwaitQuit();
  }

  emulator.reset();
  Profiler::Dump();
  Profiler::Shutdown();
  return 0;
}

}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia", L"xenia some.xex", xe::xenia_main);
