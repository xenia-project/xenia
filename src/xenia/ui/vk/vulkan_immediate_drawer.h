/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VK_VULKAN_IMMEDIATE_DRAWER_H_
#define XENIA_UI_VK_VULKAN_IMMEDIATE_DRAWER_H_

#include <memory>

#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/vk/transient_objects.h"

namespace xe {
namespace ui {
namespace vk {

class VulkanContext;

class VulkanImmediateDrawer : public ImmediateDrawer {
 public:
  VulkanImmediateDrawer(VulkanContext* graphics_context);
  ~VulkanImmediateDrawer() override;

  bool Initialize();
  void Shutdown();

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

  struct PushConstants {
    struct {
      float viewport_inv_size[2];
    } vertex;
    struct {
      uint32_t restrict_texture_samples;
    } fragment;
  };
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;

  VkPipeline pipeline_triangle_ = VK_NULL_HANDLE;
  VkPipeline pipeline_line_ = VK_NULL_HANDLE;

  std::unique_ptr<UploadBufferChain> vertex_buffer_chain_ = nullptr;

  VkCommandBuffer current_command_buffer_ = VK_NULL_HANDLE;
  VkExtent2D current_render_target_extent_;
  bool batch_open_ = false;
  bool batch_has_index_buffer_;
  VkPipeline current_pipeline_ = VK_NULL_HANDLE;
};

}  // namespace vk
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VK_VULKAN_IMMEDIATE_DRAWER_H_
