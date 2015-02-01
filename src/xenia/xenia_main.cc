/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <gflags/gflags.h>
#include "poly/main.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel.h"
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
  if (FLAGS_target.size() && args.size() >= 2) {
    std::wstring path;
    if (FLAGS_target.size()) {
      // Passed as a named argument.
      // TODO(benvanik): find something better than gflags that supports
      // unicode.
      path = poly::to_wstring(FLAGS_target);
    } else {
      // Passed as an unnamed argument.
      path = args[1];
    }
    // Normalize the path and make absolute.
    std::wstring abs_path = poly::to_absolute_path(path);

    result = emulator->main_window()->LaunchPath(abs_path);
    if (XFAILED(result)) {
      XELOGE("Failed to launch target: %.8X", result);
      return 1;
    }
  }

  // Wait until we are exited.
  emulator->main_window()->loop()->AwaitQuit();

  emulator.reset();
  Profiler::Dump();
  Profiler::Shutdown();
  return 0;
}

}  // namespace xe

DEFINE_ENTRY_POINT(L"xenia", L"xenia some.xex", xe::xenia_main);
