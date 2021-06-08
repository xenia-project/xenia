/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_SHADER_H_
#define XENIA_GPU_VULKAN_VULKAN_SHADER_H_

#include <cstdint>

#include "xenia/gpu/shader.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanShader : public Shader {
 public:
  class VulkanTranslation : public Translation {
   public:
    VulkanTranslation(VulkanShader& shader, uint64_t modification)
        : Translation(shader, modification) {}
    ~VulkanTranslation() override;

    VkShaderModule GetOrCreateShaderModule();
    VkShaderModule shader_module() const { return shader_module_; }

   private:
    VkShaderModule shader_module_ = VK_NULL_HANDLE;
  };

  VulkanShader(xenos::ShaderType shader_type, uint64_t data_hash,
               const uint32_t* dword_ptr, uint32_t dword_count,
               const ui::vulkan::VulkanProvider& provider);

 protected:
  Translation* CreateTranslationInstance(uint64_t modification) override;

 private:
  const ui::vulkan::VulkanProvider& provider_;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_SHADER_H_
