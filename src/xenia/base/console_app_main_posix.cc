/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <string>
#include <vector>

#include "xenia/base/console_app_main.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"

extern "C" int main(int argc, char** argv) {
  xe::ConsoleAppEntryInfo entry_info = xe::GetConsoleAppEntryInfo();

  if (!entry_info.transparent_options) {
    cvar::ParseLaunchArguments(argc, argv, entry_info.positional_usage,
                               entry_info.positional_options);
  }

  // Initialize logging. Needs parsed cvars.
  // xe::InitializeLogging(entry_info.name);

  std::vector<std::string> args;
  for (int n = 0; n < argc; n++) {
    args.emplace_back(argv[n]);
  }

  int result = entry_info.entry_point(args);

  // xe::ShutdownLogging();

  return result;
}
