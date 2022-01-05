/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/main_android.h"

#include <cstddef>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/base/threading.h"

namespace xe {

static size_t android_initializations_ = 0;

static int32_t android_api_level_ = __ANDROID_API__;

void InitializeAndroidAppFromMainThread(int32_t api_level) {
  if (android_initializations_++) {
    // Already initialized for another component in the process.
    return;
  }

  // Set the API level before everything else if available - may be needed by
  // subsystem initialization itself.
  android_api_level_ = api_level;

  // Logging uses threading.
  xe::threading::AndroidInitialize();

  // Multiple apps can be launched within one process - don't pass the actual
  // app name.
  xe::InitializeLogging("xenia");

  xe::memory::AndroidInitialize();
}

void ShutdownAndroidAppFromMainThread() {
  assert_not_zero(android_initializations_);
  if (!android_initializations_) {
    return;
  }
  if (--android_initializations_) {
    // Other components are still running.
    return;
  }

  xe::memory::AndroidShutdown();

  xe::ShutdownLogging();

  xe::threading::AndroidShutdown();

  android_api_level_ = __ANDROID_API__;
}

int32_t GetAndroidApiLevel() { return android_api_level_; }

}  // namespace xe
