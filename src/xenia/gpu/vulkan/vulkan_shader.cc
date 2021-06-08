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

#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace gpu {
namespace vulkan {

VulkanShader::VulkanTranslation::~VulkanTranslation() {
  if (shader_module_) {
    const ui::vulkan::VulkanProvider& provider =
        static_cast<const VulkanShader&>(shader()).provider_;
    provider.dfn().vkDestroyShaderModule(provider.device(), shader_module_,
                                         nullptr);
  }
}

VkShaderModule VulkanShader::VulkanTranslation::GetOrCreateShaderModule() {
  if (!is_valid()) {
    return VK_NULL_HANDLE;
  }
  if (shader_module_ != VK_NULL_HANDLE) {
    return shader_module_;
  }
  const ui::vulkan::VulkanProvider& provider =
      static_cast<const VulkanShader&>(shader()).provider_;
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
    MakeInvalid();
    return VK_NULL_HANDLE;
  }
  return shader_module_;
}

VulkanShader::VulkanShader(xenos::ShaderType shader_type, uint64_t data_hash,
                           const uint32_t* dword_ptr, uint32_t dword_count,
                           const ui::vulkan::VulkanProvider& provider)
    : Shader(shader_type, data_hash, dword_ptr, dword_count),
      provider_(provider) {}

Shader::Translation* VulkanShader::CreateTranslationInstance(
    uint64_t modification) {
  return new VulkanTranslation(*this, modification);
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
