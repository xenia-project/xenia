/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_TEXTURE_CACHE_H_
#define XENIA_GPU_VULKAN_TEXTURE_CACHE_H_

#include "xenia/gpu/register_file.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace gpu {
namespace vulkan {

//
class TextureCache {
 public:
  TextureCache(RegisterFile* register_file, ui::vulkan::VulkanDevice* device);
  ~TextureCache();

  // Descriptor set layout containing all possible texture bindings.
  // The set contains one descriptor for each texture sampler [0-31].
  VkDescriptorSetLayout texture_descriptor_set_layout() const {
    return texture_descriptor_set_layout_;
  }

  // TODO(benvanik): UploadTexture.
  // TODO(benvanik): Resolve.
  // TODO(benvanik): ReadTexture.

  // Clears all cached content.
  void ClearCache();

 private:
  RegisterFile* register_file_ = nullptr;
  VkDevice device_ = nullptr;

  VkDescriptorPool descriptor_pool_ = nullptr;
  VkDescriptorSetLayout texture_descriptor_set_layout_ = nullptr;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_TEXTURE_CACHE_H_
