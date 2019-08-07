/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VK_VULKAN_UTIL_H_
#define XENIA_UI_VK_VULKAN_UTIL_H_

#include "xenia/ui/vk/vulkan_provider.h"

namespace xe {
namespace ui {
namespace vk {
namespace util {

template <typename F, typename T>
inline bool DestroyAndNullHandle(F* destroy_function, T& handle) {
  if (handle != VK_NULL_HANDLE) {
    destroy_function(handle, nullptr);
    handle = VK_NULL_HANDLE;
    return true;
  }
  return false;
}

}  // namespace util
}  // namespace vk
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VK_VULKAN_UTIL_H_
