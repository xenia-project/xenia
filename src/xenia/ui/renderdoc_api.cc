/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/renderdoc_api.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"

#if XE_PLATFORM_LINUX
#include <dlfcn.h>
#elif XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

namespace xe {
namespace ui {

bool RenderdocApi::Initialize() {
  Shutdown();
  pRENDERDOC_GetAPI get_api = nullptr;
  // The RenderDoc library should be already loaded into the process if
  // RenderDoc is attached - this is why RTLD_NOLOAD or GetModuleHandle instead
  // of LoadLibrary.
#if XE_PLATFORM_LINUX
#if XE_PLATFORM_ANDROID
  const char* librenderdoc_name = "libVkLayer_GLES_RenderDoc.so";
#else
  const char* librenderdoc_name = "librenderdoc.so";
#endif
  library_ = dlopen(librenderdoc_name, RTLD_NOW | RTLD_NOLOAD);
  if (library_) {
    get_api = pRENDERDOC_GetAPI(dlsym(library_, "RENDERDOC_GetAPI"));
  }
#elif XE_PLATFORM_WIN32
  library_ = GetModuleHandleA("renderdoc.dll");
  if (library_) {
    get_api = pRENDERDOC_GetAPI(
        GetProcAddress(HMODULE(library_), "RENDERDOC_GetAPI"));
  }
#endif
  if (!get_api) {
    Shutdown();
    return false;
  }
  // get_api will be null if RenderDoc is not attached, or the API isn't
  // available on this platform, or there was an error.
  if (!get_api || !get_api(eRENDERDOC_API_Version_1_0_0, (void**)&api_1_0_0_) ||
      !api_1_0_0_) {
    Shutdown();
    return false;
  }
  XELOGI("RenderDoc API initialized");
  return true;
}

void RenderdocApi::Shutdown() {
  api_1_0_0_ = nullptr;
  if (library_) {
#if XE_PLATFORM_LINUX
    dlclose(library_);
#endif
    // Not calling FreeLibrary on Windows as GetModuleHandle doesn't increment
    // the reference count.
    library_ = nullptr;
  }
}

}  // namespace ui
}  // namespace xe
