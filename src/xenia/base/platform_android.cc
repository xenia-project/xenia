/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/platform_android.h"

#include <android/configuration.h>
#include <dlfcn.h>

#include "xenia/base/assert.h"

namespace xe {
namespace platform {
namespace android {

static bool initialized = false;

static int32_t api_level_ = __ANDROID_API__;

static ApiFunctions api_functions_;

void Initialize(const ANativeActivity* activity) {
  if (initialized) {
    return;
  }

  AConfiguration* configuration = AConfiguration_new();
  AConfiguration_fromAssetManager(configuration, activity->assetManager);
  api_level_ = AConfiguration_getSdkVersion(configuration);
  AConfiguration_delete(configuration);

  if (api_level_ >= 26) {
    // Leaked intentionally, already loaded into the address space.
    // https://chromium.googlesource.com/chromium/src/+/master/third_party/ashmem/ashmem-dev.c#201
    void* libandroid = dlopen("libandroid.so", RTLD_NOW);
    assert_not_null(libandroid);
#define XE_PLATFORM_ANDROID_LOAD_API_FUNCTION(name, api)         \
  api_functions_.api_##api.name =                                \
      reinterpret_cast<decltype(api_functions_.api_##api.name)>( \
          dlsym(libandroid, #name));                             \
  assert_not_null(api_functions_.api_##api.name);
    XE_PLATFORM_ANDROID_LOAD_API_FUNCTION(ASharedMemory_create, 26);
#undef XE_PLATFORM_ANDROID_LOAD_API_FUNCTION
  }

  initialized = true;
}

int32_t api_level() { return api_level_; }

const ApiFunctions& api_functions() { return api_functions_; }

}  // namespace android
}  // namespace platform
}  // namespace xe
