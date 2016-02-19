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

  // TODO(benvanik): UploadTexture.
  // TODO(benvanik): Resolve.
  // TODO(benvanik): ReadTexture.

  // Clears all cached content.
  void ClearCache();

 private:
  RegisterFile* register_file_ = nullptr;
  VkDevice device_ = nullptr;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_TEXTURE_CACHE_H_
