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
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

using xe::ui::vulkan::CheckResult;

VulkanShader::VulkanShader(ui::vulkan::VulkanDevice* device,
                           xenos::ShaderType shader_type, uint64_t data_hash,
                           const uint32_t* dword_ptr, uint32_t dword_count)
    : Shader(shader_type, data_hash, dword_ptr, dword_count), device_(device) {}

VulkanShader::VulkanTranslation::~VulkanTranslation() {
  if (shader_module_) {
    const VulkanShader& vulkan_shader = static_cast<VulkanShader&>(shader());
    const ui::vulkan::VulkanDevice* device = vulkan_shader.device_;
    const ui::vulkan::VulkanDevice::DeviceFunctions& dfn = device->dfn();
    dfn.vkDestroyShaderModule(*device, shader_module_, nullptr);
    shader_module_ = nullptr;
  }
}

bool VulkanShader::VulkanTranslation::Prepare() {
  assert_null(shader_module_);
  assert_true(is_valid());

  const VulkanShader& vulkan_shader = static_cast<VulkanShader&>(shader());
  const ui::vulkan::VulkanDevice* device = vulkan_shader.device_;
  const ui::vulkan::VulkanDevice::DeviceFunctions& dfn = device->dfn();

  // Create the shader module.
  VkShaderModuleCreateInfo shader_info;
  shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_info.pNext = nullptr;
  shader_info.flags = 0;
  shader_info.codeSize = translated_binary().size();
  shader_info.pCode =
      reinterpret_cast<const uint32_t*>(translated_binary().data());
  auto status =
      dfn.vkCreateShaderModule(*device, &shader_info, nullptr, &shader_module_);
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
  device->DbgSetObjectName(uint64_t(shader_module_),
                           VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT,
                           fmt::format("S({}): {:016X}", type_char,
                                       vulkan_shader.ucode_data_hash()));
  return status == VK_SUCCESS;
}

Shader::Translation* VulkanShader::CreateTranslationInstance(
    uint64_t modification) {
  return new VulkanTranslation(*this, modification);
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
