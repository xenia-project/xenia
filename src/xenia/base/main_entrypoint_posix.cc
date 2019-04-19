/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/main.h"

#include <gflags/gflags.h>

#include "xenia/base/logging.h"
#include "xenia/base/string.h"

extern "C" int main(int argc, char** argv) {
  auto entry_info = xe::GetEntryInfo();

  google::SetUsageMessage(std::string("usage: ") +
                          xe::to_string(entry_info.usage));
  google::SetVersionString("1.0");
  google::ParseCommandLineFlags(&argc, &argv, true);

  std::vector<std::wstring> args;
  for (int n = 0; n < argc; n++) {
    args.push_back(xe::to_wstring(argv[n]));
  }

  // Initialize logging. Needs parsed FLAGS.
  xe::InitializeLogging(entry_info.name);

  // Call app-provided entry point.
  int result = entry_info.entry_point(args);

  google::ShutDownCommandLineFlags();
  return result;
}
