/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "build/version.h"
#include "xenia/base/cvar.h"
#include "xenia/base/main.h"

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/string.h"

namespace xe {

bool has_console_attached() { return true; }

}  // namespace xe

extern "C" int main(int argc, char** argv) {
  auto entry_info = xe::GetEntryInfo();

  cvar::ParseLaunchArguments(argc, argv, entry_info.positional_usage,
                             entry_info.positional_options);

  std::vector<std::string> args;
  for (int n = 0; n < argc; n++) {
    args.push_back(argv[n]);
  }

  // Initialize logging. Needs parsed FLAGS.
  xe::InitializeLogging(entry_info.name);

  // Print version info.
  XELOGI("Build: {} / {} on {}", XE_BUILD_BRANCH, XE_BUILD_COMMIT,
         XE_BUILD_DATE);

  // Call app-provided entry point.
  int result = entry_info.entry_point(args);

  return result;
}
