/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <poly/main.h>

#include <gflags/gflags.h>
#include <poly/string.h>

namespace poly {

bool has_console_attached() { return true; }

}  // namespace poly

extern "C" int main(int argc, char** argv) {
  auto entry_info = poly::GetEntryInfo();

  google::SetUsageMessage(std::string("usage: ") +
                          poly::to_string(entry_info.usage));
  google::SetVersionString("1.0");
  google::ParseCommandLineFlags(&argc, &argv, true);

  std::vector<std::wstring> args;
  for (int n = 0; n < argc; n++) {
    args.push_back(poly::to_wstring(argv[n]));
  }

  // Call app-provided entry point.
  int result = entry_info.entry_point(args);

  google::ShutDownCommandLineFlags();
  return result;
}
