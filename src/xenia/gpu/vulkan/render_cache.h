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
#include "xenia/gpu/vulkan/vulkan_shader.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace gpu {
namespace vulkan {

// Manages the virtualized EDRAM and the render target cache.
//
// On the 360 the render target is an opaque block of memory in EDRAM that's
// only accessible via resolves. We use this to our advantage to simulate
// something like it as best we can by having a shared backing memory with
// a multitude of views for each tile location in EDRAM.
//
// This allows us to have the same base address write to the same memory
// regardless of framebuffer format. Resolving then uses whatever format the
// resolve requests straight from the backing memory.
class RenderCache {
 public:
  RenderCache(RegisterFile* register_file, ui::vulkan::VulkanDevice* device);
  ~RenderCache();

  // Begins a render pass targeting the state-specified framebuffer formats.
  // The command buffer will be transitioned into the render pass phase.
  VkRenderPass BeginRenderPass(VkCommandBuffer command_buffer,
                               VulkanShader* vertex_shader,
                               VulkanShader* pixel_shader);

  // Ends the current render pass.
  // The command buffer will be transitioned out of the render pass phase.
  void EndRenderPass();

  // Clears all cached content.
  void ClearCache();

 private:
  RegisterFile* register_file_ = nullptr;
  VkDevice device_ = nullptr;

  // Only valid during a BeginRenderPass/EndRenderPass block.
  VkCommandBuffer current_command_buffer_ = nullptr;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_RENDER_CACHE_H_
