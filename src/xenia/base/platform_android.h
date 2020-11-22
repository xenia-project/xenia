/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_PLATFORM_ANDROID_H_
#define XENIA_BASE_PLATFORM_ANDROID_H_

// NOTE: if you're including this file it means you are explicitly depending
// on Android-specific headers. This is bad for portability and should be
// avoided!

#include <android/native_activity.h>
#include <pthread.h>
#include <cstddef>
#include <cstdint>

namespace xe {
namespace platform {
namespace android {

// Must be called in onCreate of the first activity.
void Initialize(const ANativeActivity* activity);

// Returns the device API level - if not initialized, will return the minimum
// level supported by Xenia.
int32_t api_level();

// Android API functions added after the minimum supported API version.
struct ApiFunctions {
  struct {
    // libandroid
    int (*ASharedMemory_create)(const char* name, size_t size);
    // libc
    int (*pthread_getname_np)(pthread_t pthread, char* buf, size_t n);
  } api_26;
};
const ApiFunctions& api_functions();

}  // namespace android
}  // namespace platform
}  // namespace xe

#endif  // XENIA_BASE_PLATFORM_ANDROID_H_
