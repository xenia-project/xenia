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

#include <map>
#include <memory>

#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace ui {
namespace vulkan {

class DescriptorPool;

class Blitter {
 public:
  Blitter();
  ~Blitter();

  bool Initialize(VulkanDevice* device);
  void Scavenge();
  void Shutdown();

  // Queues commands to blit a texture to another texture.
  // dst_framebuffer must only have one attachment, the target texture.
  void BlitTexture2D(VkCommandBuffer command_buffer, VkFence fence,
                     VkImageView src_image_view, VkRect2D src_rect,
                     VkExtent2D src_extents, VkFormat dst_image_format,
                     VkOffset2D dst_offset, VkExtent2D dst_extents,
                     VkFramebuffer dst_framebuffer, VkFilter filter,
                     bool color_or_depth, bool swap_channels);

  void CopyColorTexture2D(VkCommandBuffer command_buffer, VkFence fence,
                          VkImage src_image, VkImageView src_image_view,
                          VkOffset2D src_offset, VkImage dst_image,
                          VkImageView dst_image_view, VkExtent2D extents,
                          VkFilter filter, bool swap_channels);
  void CopyDepthTexture(VkCommandBuffer command_buffer, VkFence fence,
                        VkImage src_image, VkImageView src_image_view,
                        VkOffset2D src_offset, VkImage dst_image,
                        VkImageView dst_image_view, VkExtent2D extents);

  // For framebuffer creation.
  VkRenderPass GetRenderPass(VkFormat format);

 private:
  struct VtxPushConstants {
    float src_uv[4];  // 0x00
  };

  struct PixPushConstants {
    int _pad[3];  // 0x10
    int swap;     // 0x1C
  };

  VkPipeline GetPipeline(VkRenderPass render_pass, VkShaderModule frag_shader);
  VkRenderPass CreateRenderPass(VkFormat output_format);
  VkPipeline CreatePipeline(VkRenderPass render_pass,
                            VkShaderModule frag_shader);

  std::unique_ptr<DescriptorPool> descriptor_pool_ = nullptr;
  VulkanDevice* device_ = nullptr;
  VkPipeline pipeline_color_ = nullptr;
  VkPipeline pipeline_depth_ = nullptr;
  VkPipelineLayout pipeline_layout_ = nullptr;
  VkShaderModule blit_vertex_ = nullptr;
  VkShaderModule blit_color_ = nullptr;
  VkShaderModule blit_depth_ = nullptr;
  VkSampler samp_linear_ = nullptr;
  VkSampler samp_nearest_ = nullptr;
  VkDescriptorSetLayout descriptor_set_layout_ = nullptr;

  std::map<VkFormat, VkRenderPass> render_passes_;
  std::map<std::pair<VkRenderPass, VkShaderModule>, VkPipeline> pipelines_;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_BLITTER_H_
