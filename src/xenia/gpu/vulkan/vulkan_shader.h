/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_SHADER_H_
#define XENIA_GPU_VULKAN_VULKAN_SHADER_H_

#include <cstdint>

#include "xenia/gpu/spirv_shader.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace gpu {
namespace vulkan {

class VulkanShader : public SpirvShader {
 public:
  class VulkanTranslation : public SpirvTranslation {
   public:
    explicit VulkanTranslation(VulkanShader& shader, uint64_t modification)
        : SpirvTranslation(shader, modification) {}
    ~VulkanTranslation() override;

    VkShaderModule GetOrCreateShaderModule();
    VkShaderModule shader_module() const { return shader_module_; }

   private:
    VkShaderModule shader_module_ = VK_NULL_HANDLE;
  };

  explicit VulkanShader(const ui::vulkan::VulkanProvider& provider,
                        xenos::ShaderType shader_type, uint64_t ucode_data_hash,
                        const uint32_t* ucode_dwords, size_t ucode_dword_count,
                        std::endian ucode_source_endian = std::endian::big);

  // For owning subsystem like the pipeline cache, accessors for unique
  // identifiers (used instead of hashes to make sure collisions can't happen)
  // of binding layouts used by the shader, for invalidation if a shader with an
  // incompatible layout has been bound.
  size_t GetTextureBindingLayoutUserUID() const {
    return texture_binding_layout_user_uid_;
  }
  size_t GetSamplerBindingLayoutUserUID() const {
    return sampler_binding_layout_user_uid_;
  }
  // Modifications of the same shader can be translated on different threads.
  // The "set" function must only be called if "enter" returned true - these are
  // set up only once.
  bool EnterBindingLayoutUserUIDSetup() {
    return !binding_layout_user_uids_set_up_.test_and_set();
  }
  void SetTextureBindingLayoutUserUID(size_t uid) {
    texture_binding_layout_user_uid_ = uid;
  }
  void SetSamplerBindingLayoutUserUID(size_t uid) {
    sampler_binding_layout_user_uid_ = uid;
  }

 protected:
  Translation* CreateTranslationInstance(uint64_t modification) override;

 private:
  const ui::vulkan::VulkanProvider& provider_;

  std::atomic_flag binding_layout_user_uids_set_up_ = ATOMIC_FLAG_INIT;
  size_t texture_binding_layout_user_uid_ = 0;
  size_t sampler_binding_layout_user_uid_ = 0;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_SHADER_H_
