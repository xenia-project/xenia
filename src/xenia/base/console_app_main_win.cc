/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstdlib>
#include <vector>

#include "xenia/base/console_app_main.h"
#include "xenia/base/main_win.h"

// A wide character entry point is required for functions like _get_wpgmptr.
int wmain(int argc_ignored, wchar_t** argv_ignored) {
  xe::ConsoleAppEntryInfo entry_info = xe::GetConsoleAppEntryInfo();

  std::vector<std::string> args;
  if (!xe::ParseWin32LaunchArguments(entry_info.transparent_options,
                                     entry_info.positional_usage,
                                     entry_info.positional_options, &args)) {
    return EXIT_FAILURE;
  }

  int result = xe::InitializeWin32App(entry_info.name);
  if (result) {
    return result;
  }

  result = entry_info.entry_point(args);

  xe::ShutdownWin32App();

  return result;
}
