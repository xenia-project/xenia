/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_MAIN_ANDROID_H_
#define XENIA_BASE_MAIN_ANDROID_H_

#include <jni.h>
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
// must be called in `main`, with a null main thread JNI environment.
void InitializeAndroidAppFromMainThread(int32_t api_level,
                                        JNIEnv* main_thread_jni_env,
                                        jobject application_context);
void ShutdownAndroidAppFromMainThread();

// May be the minimum supported level if the initialization was done without a
// configuration.
int32_t GetAndroidApiLevel();

// May return null if not in a Java VM process, or in case of a failure to
// attach on a non-main thread.
JNIEnv* GetAndroidThreadJNIEnv();
// Returns the global reference if in an application context, or null otherwise.
// This is the application context, not the activity one, because multiple
// activities may be running in one process.
jobject GetAndroidApplicationContext();

}  // namespace xe

#endif  // XENIA_BASE_MAIN_ANDROID_H_
