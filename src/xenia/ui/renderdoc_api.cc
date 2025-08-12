/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/renderdoc_api.h"

#include "xenia/base/logging.h"
#include "xenia/base/platform.h"

#if XE_PLATFORM_LINUX
#include <dlfcn.h>
#elif XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

namespace xe {
namespace ui {

std::unique_ptr<RenderDocAPI> RenderDocAPI::CreateIfConnected() {
  std::unique_ptr<RenderDocAPI> renderdoc_api(new RenderDocAPI());

  pRENDERDOC_GetAPI get_api = nullptr;

  // The RenderDoc library should already be loaded into the process if
  // RenderDoc is attached - this is why RTLD_NOLOAD or GetModuleHandle instead
  // of LoadLibrary.
#if XE_PLATFORM_LINUX
#if XE_PLATFORM_ANDROID
  const char* const library_name = "libVkLayer_GLES_RenderDoc.so";
#else
  const char* const library_name = "librenderdoc.so";
#endif
  renderdoc_api->library_ = dlopen(library_name, RTLD_NOW | RTLD_NOLOAD);
  if (!renderdoc_api->library_) {
    return nullptr;
  }
  get_api =
      pRENDERDOC_GetAPI(dlsym(renderdoc_api->library_, "RENDERDOC_GetAPI"));
#elif XE_PLATFORM_WIN32
  renderdoc_api->library_ = GetModuleHandleW(L"renderdoc.dll");
  if (!renderdoc_api->library_) {
    return nullptr;
  }
  get_api = pRENDERDOC_GetAPI(
      GetProcAddress(renderdoc_api->library_, "RENDERDOC_GetAPI"));
#endif

  // get_api will be null if RenderDoc is not connected, or the API isn't
  // available on this platform, or there was an error.
  if (!get_api ||
      !get_api(eRENDERDOC_API_Version_1_0_0,
               (void**)&renderdoc_api->api_1_0_0_) ||
      !renderdoc_api->api_1_0_0_) {
    return nullptr;
  }

  XELOGI("RenderDoc API initialized");

  return renderdoc_api;
}

RenderDocAPI::~RenderDocAPI() {
#if XE_PLATFORM_LINUX
  if (library_) {
    dlclose(library_);
  }
#endif
  // Not calling FreeLibrary on Windows as GetModuleHandle doesn't increment
  // the reference count.
}

}  // namespace ui
}  // namespace xe
