/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_WINE_UTILS_H_
#define XENIA_BASE_WINE_UTILS_H_

#include "xenia/base/platform.h"

namespace xe::platform {

#if XE_PLATFORM_WIN32
bool IsRunningUnderWine();
bool IsWineHostDarwin();
#else
inline bool IsRunningUnderWine() { return false; }
inline bool IsWineHostDarwin() { return false; }
#endif

inline bool IsWineOnDarwin() {
  return IsRunningUnderWine() && IsWineHostDarwin();
}

}  // namespace xe::platform

#endif  // XENIA_BASE_WINE_UTILS_H_
