/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_SHADER_H_
#define XENIA_GPU_VULKAN_VULKAN_SHADER_H_

#include <string>

#include "xenia/gpu/shader.h"
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

    bool Prepare();

    // Available only if the translation is_valid and has been prepared.
    VkShaderModule shader_module() const { return shader_module_; }

   private:
    VkShaderModule shader_module_ = nullptr;
  };

  VulkanShader(const ui::vulkan::VulkanProvider& provider,
               xenos::ShaderType shader_type, uint64_t ucode_data_hash,
               const uint32_t* ucode_dwords, size_t ucode_dword_count,
               std::endian ucode_source_endian = std::endian::big);

 protected:
  Translation* CreateTranslationInstance(uint64_t modification) override;

 private:
  const ui::vulkan::VulkanProvider& provider_;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_SHADER_H_
