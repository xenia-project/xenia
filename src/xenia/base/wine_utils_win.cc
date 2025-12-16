/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/wine_utils.h"

#if XE_PLATFORM_WIN32

#include <cstring>

#include "xenia/base/platform_win.h"

namespace xe::platform {

namespace {

bool HasWineExports() {
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (!ntdll) {
    return false;
  }

  // Wine typically exports these from ntdll.
  // We check either to avoid coupling to a specific Wine version.
  if (GetProcAddress(ntdll, "wine_get_host_version")) {
    return true;
  }
  if (GetProcAddress(ntdll, "wine_get_version")) {
    return true;
  }

  return false;
}

}  // namespace

bool IsRunningUnderWine() {
  static bool cached_result = HasWineExports();
  return cached_result;
}

bool IsWineHostDarwin() {
  static bool cached_result = []() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
      return false;
    }

    using wine_get_host_version_t =
        void (*)(const char** sysname, const char** release);
    auto wine_get_host_version = reinterpret_cast<wine_get_host_version_t>(
        GetProcAddress(ntdll, "wine_get_host_version"));
    if (!wine_get_host_version) {
      return false;
    }

    const char* sysname = nullptr;
    const char* release = nullptr;
    wine_get_host_version(&sysname, &release);

    if (!sysname) {
      return false;
    }

    return _stricmp(sysname, "Darwin") == 0;
  }();

  return cached_result;
}

}  // namespace xe::platform

#endif  // XE_PLATFORM_WIN32
