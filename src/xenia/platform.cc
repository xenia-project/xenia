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


#if XE_LIKE_WIN32 && defined(UNICODE) && UNICODE

int xe_main_thunk(
    int argc, wchar_t* argv[],
    void* user_main, const char* usage) {
  google::SetUsageMessage(std::string("usage: ") + usage);
  google::SetVersionString("1.0");

  int argca = argc;
  char** argva = (char**)alloca(sizeof(char*) * argca);
  for (int n = 0; n < argca; n++) {
    size_t len = xestrlenw(argv[n]);
    argva[n] = (char*)alloca(len + 1);
    xestrnarrow(argva[n], len + 1, argv[n]);
  }

  google::ParseCommandLineFlags(&argc, &argva, true);
  
  // Parse may have deleted flags - so widen again.
  int argcw = argc;
  wchar_t** argvw = (wchar_t**)alloca(sizeof(wchar_t*) * argca);
  for (int n = 0; n < argc; n++) {
    size_t len = xestrlena(argva[n]);
    argvw[n] = (wchar_t*)alloca(sizeof(wchar_t) * (len + 1));
    xestrwiden(argvw[n], len + 1, argva[n]);
  }

  int result = ((user_main_t)user_main)(argcw, (xechar_t**)argvw);
  google::ShutDownCommandLineFlags();
  return result;
}

#else

int xe_main_thunk(
    int argc, char** argv,
    void* user_main, const char* usage) {
  google::SetUsageMessage(std::string("usage: ") + usage);
  google::SetVersionString("1.0");
  google::ParseCommandLineFlags(&argc, &argv, true);
  int result = ((user_main_t)user_main)(argc, argv);
  google::ShutDownCommandLineFlags();
  return result;
}

#endif  // WIN32
