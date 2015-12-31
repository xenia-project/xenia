/**
******************************************************************************
* Xenia : Xbox 360 Emulator Research Project                                 *
******************************************************************************
* Copyright 2015 Ben Vanik. All rights reserved.                             *
* Released under the BSD license - see LICENSE in the root for more details. *
******************************************************************************
*/

#include <gflags/gflags.h>

#include <codecvt>
#include <cstring>
#include <locale>
#include <string>
#include <vector>

#define CATCH_CONFIG_RUNNER
#include "third_party/catch/include/catch.hpp"

namespace xe {
bool has_console_attached() { return true; }
}  // namespace xe

// Used in console mode apps; automatically picked based on subsystem.
int main(int argc, wchar_t* argv[]) {
  google::SetUsageMessage(std::string("usage: ..."));
  google::SetVersionString("1.0");

  // Convert all args to narrow, as gflags doesn't support wchar.
  int argca = argc;
  char** argva = (char**)alloca(sizeof(char*) * argca);
  for (int n = 0; n < argca; n++) {
    size_t len = wcslen(argv[n]);
    argva[n] = (char*)alloca(len + 1);
    wcstombs_s(nullptr, argva[n], len + 1, argv[n], _TRUNCATE);
  }

  // Parse flags; this may delete some of them.
  google::ParseCommandLineFlags(&argc, &argva, true);

#if _WIN32
  // Setup COM on the main thread.
  // NOTE: this may fail if COM has already been initialized - that's OK.
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif  // _WIN32

  // Run Catch.
  int result = Catch::Session().run(argc, argva);

  google::ShutDownCommandLineFlags();
  return result;
}
