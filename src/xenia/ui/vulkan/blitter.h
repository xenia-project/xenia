/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_BLITTER_H_
#define XENIA_UI_VULKAN_BLITTER_H_

#include <memory>

#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace ui {
namespace vulkan {

class Blitter {
 public:
  Blitter();
  ~Blitter();

  bool Initialize(VulkanDevice* device);
  void Shutdown();

  // Queues commands to blit a texture to another texture.
  void BlitTexture2D(VkCommandBuffer command_buffer, VkImage src_image,
                     VkImageView src_image_view, VkOffset2D src_offset,
                     VkExtent2D src_extents, VkImage dst_image,
                     VkImageView dst_image_view, VkFilter filter,
                     bool swap_channels);

  void CopyColorTexture2D(VkCommandBuffer command_buffer, VkImage src_image,
                          VkImageView src_image_view, VkOffset2D src_offset,
                          VkImage dst_image, VkImageView dst_image_view,
                          VkExtent2D extents, VkFilter filter,
                          bool swap_channels);
  void CopyDepthTexture(VkCommandBuffer command_buffer, VkImage src_image,
                        VkImageView src_image_view, VkOffset2D src_offset,
                        VkImage dst_image, VkImageView dst_image_view,
                        VkExtent2D extents);

 private:
  VkRenderPass CreateRenderPass(VkFormat output_format);
  VkPipeline CreatePipeline(VkRenderPass render_pass);

  VulkanDevice* device_ = nullptr;
  VkPipeline pipeline_color_ = nullptr;
  VkPipeline pipeline_depth_ = nullptr;
  VkPipelineLayout pipeline_layout_ = nullptr;
  VkShaderModule blit_vertex_ = nullptr;
  VkShaderModule blit_color_ = nullptr;
  VkShaderModule blit_depth_ = nullptr;
  VkDescriptorSetLayout descriptor_set_layout_ = nullptr;
  VkRenderPass render_pass_ = nullptr;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_BLITTER_H_
