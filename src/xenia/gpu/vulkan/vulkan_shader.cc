/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_shader.h"

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

using xe::ui::vulkan::util::CheckResult;

VulkanShader::VulkanShader(const ui::vulkan::VulkanProvider& provider,
                           xenos::ShaderType shader_type,
                           uint64_t ucode_data_hash,
                           const uint32_t* ucode_dwords,
                           size_t ucode_dword_count,
                           std::endian ucode_source_endian)
    : Shader(shader_type, ucode_data_hash, ucode_dwords, ucode_dword_count,
             ucode_source_endian),
      provider_(provider) {}

VulkanShader::VulkanTranslation::~VulkanTranslation() {
  if (shader_module_) {
    const ui::vulkan::VulkanProvider& provider =
        static_cast<VulkanShader&>(shader()).provider_;
    const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
    VkDevice device = provider.device();
    dfn.vkDestroyShaderModule(device, shader_module_, nullptr);
    shader_module_ = nullptr;
  }
}

bool VulkanShader::VulkanTranslation::Prepare() {
  assert_null(shader_module_);
  assert_true(is_valid());

  const VulkanShader& vulkan_shader = static_cast<VulkanShader&>(shader());
  const ui::vulkan::VulkanProvider& provider = vulkan_shader.provider_;
  const ui::vulkan::VulkanProvider::DeviceFunctions& dfn = provider.dfn();
  VkDevice device = provider.device();

  // Create the shader module.
  VkShaderModuleCreateInfo shader_info;
  shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_info.pNext = nullptr;
  shader_info.flags = 0;
  shader_info.codeSize = translated_binary().size();
  shader_info.pCode =
      reinterpret_cast<const uint32_t*>(translated_binary().data());
  auto status =
      dfn.vkCreateShaderModule(device, &shader_info, nullptr, &shader_module_);
  CheckResult(status, "vkCreateShaderModule");

  char type_char;
  switch (vulkan_shader.type()) {
    case xenos::ShaderType::kVertex:
      type_char = 'v';
      break;
    case xenos::ShaderType::kPixel:
      type_char = 'p';
      break;
    default:
      type_char = 'u';
  }
  provider.SetDeviceObjectName(
      VK_OBJECT_TYPE_SHADER_MODULE, uint64_t(shader_module_),
      fmt::format("S({}): {:016X}", type_char, vulkan_shader.ucode_data_hash())
          .c_str());
  return status == VK_SUCCESS;
}

Shader::Translation* VulkanShader::CreateTranslationInstance(
    uint64_t modification) {
  return new VulkanTranslation(*this, modification);
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
