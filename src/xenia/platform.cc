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

bool _has_console = true;

}  // namespace


#if XE_LIKE_WIN32 && defined(UNICODE) && UNICODE

#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <shellapi.h>

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

bool xe_has_console() {
  return _has_console;
}

void xe_attach_console() {
  bool has_console = AttachConsole(ATTACH_PARENT_PROCESS) == TRUE;
  if (!has_console) {
    // We weren't launched from a console, so just return.
    // We could alloc our own console, but meh:
    // has_console = AllocConsole() == TRUE;
    _has_console = false;
    return;
  }
  _has_console = true;

  auto std_handle = (intptr_t)GetStdHandle(STD_OUTPUT_HANDLE);
  auto con_handle = _open_osfhandle(std_handle, _O_TEXT);
  auto fp = _fdopen(con_handle, "w");
  *stdout = *fp;
  setvbuf(stdout, NULL, _IONBF, 0);

  std_handle = (intptr_t)GetStdHandle(STD_ERROR_HANDLE);
  con_handle = _open_osfhandle(std_handle, _O_TEXT);
  fp = _fdopen(con_handle, "w");
  *stderr = *fp;
  setvbuf(stderr, NULL, _IONBF, 0);
}

int xe_main_window_thunk(
    wchar_t* command_line,
    void* user_main, const wchar_t* name, const char* usage) {
  xe_attach_console();
  wchar_t buffer[2048];
  xestrcpy(buffer, XECOUNT(buffer), name);
  xestrcat(buffer, XECOUNT(buffer), L" ");
  xestrcat(buffer, XECOUNT(buffer), command_line);
  int argc;
  wchar_t** argv = CommandLineToArgvW(buffer, &argc);
  if (!argv) {
    return 1;
  }
  int result = xe_main_thunk(argc, argv, user_main, usage);
  LocalFree(argv);
  return result;
}

#else

bool xe_has_console() {
  return _has_console;
}

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
