/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/platform.h>

#include <gflags/gflags.h>

#include <xenia/common.h>


namespace {

typedef int (*user_main_t)(int argc, xechar_t** argv);

}


#if XE_PLATFORM(WIN32) && XE_WCHAR

int xe_main_thunk(
    int argc, wchar_t* argv[],
    void* user_main, const char* usage) {
  google::SetUsageMessage(std::string("usage: ") + usage);
  google::SetVersionString("1.0");

  char** argva = new char*[argc];
  for (int n = 0; n < argc; n++) {
    size_t len = xestrlenw(argv[n]);
    argva[n] = (char*)malloc(len);
    xestrnarrow(argva[n], len, argv[n]);
  }

  google::ParseCommandLineFlags(&argc, &argva, true);

  int result = ((user_main_t)user_main)(argc, (xechar_t**)argv);

  for (int n = 0; n < argc; n++) {
    free(argva[n]);
  }
  delete[] argva;

  return result;
}

#else

int xe_main_thunk(
    int argc, char** argv,
    void* user_main, const char* usage) {
  google::SetUsageMessage(std::string("usage: ") + usage);
  google::SetVersionString("1.0");
  google::ParseCommandLineFlags(&argc, &argv, true);
  return ((user_main_t)user_main)(argc, argv);
}

#endif  // WIN32
