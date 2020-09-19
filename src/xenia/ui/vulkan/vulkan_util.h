/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_UTIL_H_
#define XENIA_UI_VULKAN_VULKAN_UTIL_H_

#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace ui {
namespace vulkan {
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

template <typename F, typename P, typename T>
inline bool DestroyAndNullHandle(F* destroy_function, P parent, T& handle) {
  if (handle != VK_NULL_HANDLE) {
    destroy_function(parent, handle, nullptr);
    handle = VK_NULL_HANDLE;
    return true;
  }
  return false;
}

inline VkShaderModule CreateShaderModule(const VulkanProvider& provider,
                                         const void* code, size_t code_size) {
  VkShaderModuleCreateInfo shader_module_create_info;
  shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_module_create_info.pNext = nullptr;
  shader_module_create_info.flags = 0;
  shader_module_create_info.codeSize = code_size;
  shader_module_create_info.pCode = reinterpret_cast<const uint32_t*>(code);
  VkShaderModule shader_module;
  return provider.dfn().vkCreateShaderModule(
             provider.device(), &shader_module_create_info, nullptr,
             &shader_module) == VK_SUCCESS
             ? shader_module
             : nullptr;
}

}  // namespace util
}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_UTIL_H_
