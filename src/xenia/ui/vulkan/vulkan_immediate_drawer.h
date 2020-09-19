/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_IMMEDIATE_DRAWER_H_
#define XENIA_UI_VULKAN_VULKAN_IMMEDIATE_DRAWER_H_

#include <memory>

#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/vulkan/vulkan_upload_buffer_pool.h"

namespace xe {
namespace ui {
namespace vulkan {

class VulkanContext;

class VulkanImmediateDrawer : public ImmediateDrawer {
 public:
  VulkanImmediateDrawer(VulkanContext& graphics_context);
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
  struct PushConstants {
    struct Vertex {
      float viewport_size_inv[2];
    } vertex;
  };

  bool EnsurePipelinesCreated();

  VulkanContext& context_;

  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;

  std::unique_ptr<VulkanUploadBufferPool> vertex_buffer_pool_;

  VkFormat pipeline_framebuffer_format_ = VK_FORMAT_UNDEFINED;
  VkPipeline pipeline_triangle_ = VK_NULL_HANDLE;
  VkPipeline pipeline_line_ = VK_NULL_HANDLE;

  VkCommandBuffer current_command_buffer_ = VK_NULL_HANDLE;
  VkExtent2D current_render_target_extent_;
  VkPipeline current_pipeline_;
  bool batch_open_ = false;
  bool batch_has_index_buffer_;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_IMMEDIATE_DRAWER_H_
