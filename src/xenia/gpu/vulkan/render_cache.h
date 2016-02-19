/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_RENDER_CACHE_H_
#define XENIA_GPU_VULKAN_RENDER_CACHE_H_

#include "xenia/gpu/register_file.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace gpu {
namespace vulkan {

// Configures and caches pipelines based on render state.
// This is responsible for properly setting all state required for a draw
// including shaders, various blend/etc options, and input configuration.
class RenderCache {
 public:
  RenderCache(RegisterFile* register_file, ui::vulkan::VulkanDevice* device);
  ~RenderCache();

  VkRenderPass BeginRenderPass(VkCommandBuffer command_buffer);
  void EndRenderPass();

  // Clears all cached content.
  void ClearCache();

 private:
  RegisterFile* register_file_ = nullptr;
  VkDevice device_ = nullptr;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_RENDER_CACHE_H_
