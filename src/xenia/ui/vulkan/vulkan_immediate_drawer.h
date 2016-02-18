/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_IMMEDIATE_DRAWER_H_
#define XENIA_UI_VULKAN_VULKAN_IMMEDIATE_DRAWER_H_

#include <memory>

#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/vulkan/vulkan.h"

namespace xe {
namespace ui {
namespace vulkan {

class LightweightCircularBuffer;
class VulkanContext;

class VulkanImmediateDrawer : public ImmediateDrawer {
 public:
  VulkanImmediateDrawer(VulkanContext* graphics_context);
  ~VulkanImmediateDrawer() override;

  std::unique_ptr<ImmediateTexture> CreateTexture(uint32_t width,
                                                  uint32_t height,
                                                  ImmediateTextureFilter filter,
                                                  bool repeat,
                                                  const uint8_t* data) override;
  void UpdateTexture(ImmediateTexture* texture, const uint8_t* data) override;

  void Begin(int render_target_width, int render_target_height) override;
  void BeginDrawBatch(const ImmediateDrawBatch& batch) override;
  void Draw(const ImmediateDraw& draw) override;
  void EndDrawBatch() override;
  void End() override;

 private:
  VulkanContext* context_ = nullptr;

  struct {
    VkSampler nearest_clamp = nullptr;
    VkSampler nearest_repeat = nullptr;
    VkSampler linear_clamp = nullptr;
    VkSampler linear_repeat = nullptr;
  } samplers_;

  VkDescriptorSetLayout texture_set_layout_ = nullptr;
  VkDescriptorPool descriptor_pool_ = nullptr;
  VkPipelineLayout pipeline_layout_ = nullptr;
  VkPipeline triangle_pipeline_ = nullptr;
  VkPipeline line_pipeline_ = nullptr;

  std::unique_ptr<LightweightCircularBuffer> circular_buffer_;

  bool batch_has_index_buffer_ = false;
  VkCommandBuffer current_cmd_buffer_ = nullptr;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_IMMEDIATE_DRAWER_H_
