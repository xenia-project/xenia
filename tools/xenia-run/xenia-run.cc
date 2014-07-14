/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/xenia.h>

#include <gflags/gflags.h>


using namespace xe;


DEFINE_string(target, "",
    "Specifies the target .xex or .iso to execute.");


int xenia_run(int argc, xechar_t** argv) {
  int result_code = 1;

  Profiler::Initialize();
  Profiler::ThreadEnter("main");

  Emulator* emulator = NULL;

  // Grab path from the flag or unnamed argument.
  if (!FLAGS_target.size() && argc < 2) {
    google::ShowUsageWithFlags("xenia-run");
    XEFATAL("Pass a file to launch.");
    return 1;
  }
  const xechar_t* path = NULL;
  if (FLAGS_target.size()) {
    // Passed as a named argument.
    // TODO(benvanik): find something better than gflags that supports unicode.
    xechar_t buffer[poly::max_path];
    XEIGNORE(xestrwiden(buffer, sizeof(buffer), FLAGS_target.c_str()));
    path = buffer;
  } else {
    // Passed as an unnamed argument.
    path = argv[1];
  }

  // Create platform abstraction layer.
  xe_pal_options_t pal_options;
  xe_zero_struct(&pal_options, sizeof(pal_options));
  XEEXPECTZERO(xe_pal_init(pal_options));

  // Normalize the path and make absolute.
  // TODO(benvanik): move this someplace common.
  xechar_t abs_path[poly::max_path];
  xe_path_get_absolute(path, abs_path, XECOUNT(abs_path));

  // Grab file extension.
  // May be NULL if an STFS file.
  const xechar_t* dot = xestrrchr(abs_path, '.');

  // Create the emulator.
  emulator = new Emulator(L"");
  XEEXPECTNOTNULL(emulator);
  X_STATUS result = emulator->Setup();
  if (XFAILED(result)) {
    XELOGE("Failed to setup emulator: %.8X", result);
    XEFAIL();
  }

  // Launch based on file type.
  // This is a silly guess based on file extension.
  // NOTE: the runtime launch routine will wait until the module exits.
  if (!dot) {
    // Likely an STFS container.
    result = emulator->LaunchSTFSTitle(abs_path);
  } else if (xestrcmp(dot, L".xex") == 0) {
    // Treat as a naked xex file.
    result = emulator->LaunchXexFile(abs_path);
  } else {
    // Assume a disc image.
    result = emulator->LaunchDiscImage(abs_path);
  }
  if (XFAILED(result)) {
    XELOGE("Failed to launch target: %.8X", result);
    XEFAIL();
  }

  result_code = 0;
XECLEANUP:
  delete emulator;
  if (result_code) {
    XEFATAL("Failed to launch emulator: %d", result_code);
  }
  Profiler::Dump();
  Profiler::Shutdown();
  return result_code;
}
XE_MAIN_WINDOW_THUNK(xenia_run, L"xenia-run", "xenia-run some.xex");
