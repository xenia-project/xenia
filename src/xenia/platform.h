/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_PLATFORM_H_
#define XENIA_PLATFORM_H_

#include <poly/platform.h>

bool xe_has_console();
#if XE_LIKE_WIN32 && defined(UNICODE) && UNICODE
int xe_main_thunk(
    int argc, wchar_t* argv[],
    void* user_main, const char* usage);
#define XE_MAIN_THUNK(NAME, USAGE) \
    int wmain(int argc, wchar_t *argv[]) { \
      return xe_main_thunk(argc, argv, NAME, USAGE); \
    }
int xe_main_window_thunk(
    wchar_t* command_line,
    void* user_main, const wchar_t* name, const char* usage);
#define XE_MAIN_WINDOW_THUNK(NAME, APP_NAME, USAGE) \
    int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPTSTR command_line, int) { \
      return xe_main_window_thunk(command_line, NAME, APP_NAME, USAGE); \
    }
#else
int xe_main_thunk(
    int argc, char** argv,
    void* user_main, const char* usage);
#define XE_MAIN_THUNK(NAME, USAGE) \
    int main(int argc, char **argv) { \
      return xe_main_thunk(argc, argv, (void*)NAME, USAGE); \
    }
#define XE_MAIN_WINDOW_THUNK(NAME, APP_NAME, USAGE) \
    XE_MAIN_THUNK(NAME, USAGE)
#endif  // WIN32


#endif  // XENIA_PLATFORM_H_
