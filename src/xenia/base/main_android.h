/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_MAIN_ANDROID_H_
#define XENIA_BASE_MAIN_ANDROID_H_

#include <cstdint>

#include "xenia/base/platform.h"

namespace xe {

// In activities, these functions must be called in onCreate and onDestroy.
//
// Multiple components may run in the same process, and they will be
// instantiated in the main thread, which is, for regular applications (the
// system application exception doesn't apply to Xenia), the UI thread.
//
// For this reason, it's okay to call these functions multiple times if
// different Xenia windowed applications run in one process - there is call
// counting internally.
//
// In standalone console apps built with $(BUILD_EXECUTABLE), these functions
// must be called in `main`.
void InitializeAndroidAppFromMainThread(int32_t api_level);
void ShutdownAndroidAppFromMainThread();

// May be the minimum supported level if the initialization was done without a
// configuration.
int32_t GetAndroidApiLevel();

}  // namespace xe

#endif  // XENIA_BASE_MAIN_ANDROID_H_
