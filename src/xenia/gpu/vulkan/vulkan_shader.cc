/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_shader.h"

#include <cstdint>

namespace xe {
namespace gpu {
namespace vulkan {

VulkanShader::VulkanShader(xenos::ShaderType shader_type, uint64_t data_hash,
                           const uint32_t* dword_ptr, uint32_t dword_count)
    : Shader(shader_type, data_hash, dword_ptr, dword_count) {}

bool VulkanShader::InitializeShaderModule(
    const ui::vulkan::VulkanProvider& provider) {
  if (!is_valid()) {
    return false;
  }
  if (shader_module_ != VK_NULL_HANDLE) {
    return true;
  }
  VkShaderModuleCreateInfo shader_module_create_info;
  shader_module_create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_module_create_info.pNext = nullptr;
  shader_module_create_info.flags = 0;
  shader_module_create_info.codeSize = translated_binary().size();
  shader_module_create_info.pCode =
      reinterpret_cast<const uint32_t*>(translated_binary().data());
  if (provider.dfn().vkCreateShaderModule(provider.device(),
                                          &shader_module_create_info, nullptr,
                                          &shader_module_) != VK_SUCCESS) {
    is_valid_ = false;
    return false;
  }
  return true;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
