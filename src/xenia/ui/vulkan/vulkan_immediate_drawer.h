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

#include <cstddef>
#include <deque>
#include <memory>
#include <utility>
#include <vector>

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

  class VulkanImmediateTexture : public ImmediateTexture {
   public:
    struct Resource {
      VkImage image;
      VkDeviceMemory memory;
      VkImageView image_view;
      uint32_t descriptor_index;
    };

    VulkanImmediateTexture(uint32_t width, uint32_t height)
        : ImmediateTexture(width, height), immediate_drawer_(nullptr) {
      handle = reinterpret_cast<uintptr_t>(this);
    }
    ~VulkanImmediateTexture() override;

    // If null, this is either a blank texture, or the immediate drawer has been
    // destroyed.
    VulkanImmediateDrawer* immediate_drawer_;
    size_t immediate_drawer_index_;
    // Invalid if immediate_drawer_ is null, since it's managed by the immediate
    // drawer.
    Resource resource_;
    size_t pending_upload_index_;
    uint64_t last_usage_submission_ = 0;
  };

  struct TextureDescriptorPool {
    // Using uint64_t for recycled bits.
    static constexpr uint32_t kDescriptorCount = 64;
    VkDescriptorPool pool;
    VkDescriptorSet sets[kDescriptorCount];
    uint32_t index;
    uint32_t unallocated_count;
    uint64_t recycled_bits;
    TextureDescriptorPool* unallocated_next;
    TextureDescriptorPool* recycled_next;
  };

  bool EnsurePipelinesCreated();

  // Allocates a combined image sampler in a pool and returns its index, or
  // UINT32_MAX in case of failure.
  uint32_t AllocateTextureDescriptor();
  VkDescriptorSet GetTextureDescriptor(uint32_t descriptor_index) const;
  void FreeTextureDescriptor(uint32_t descriptor_index);

  // If data is null, a (1, 1, 1, 1) image will be created, which can be used as
  // a replacement when drawing without a real texture.
  bool CreateTextureResource(uint32_t width, uint32_t height,
                             ImmediateTextureFilter filter, bool is_repeated,
                             const uint8_t* data,
                             VulkanImmediateTexture::Resource& resource_out,
                             size_t& pending_upload_index_out);
  void DestroyTextureResource(VulkanImmediateTexture::Resource& resource);
  void OnImmediateTextureDestroyed(VulkanImmediateTexture& texture);

  VulkanContext& context_;

  // Combined image sampler pools for textures.
  VkDescriptorSetLayout texture_descriptor_set_layout_;
  std::vector<TextureDescriptorPool*> texture_descriptor_pools_;
  TextureDescriptorPool* texture_descriptor_pool_unallocated_first_ = nullptr;
  TextureDescriptorPool* texture_descriptor_pool_recycled_first_ = nullptr;

  VulkanImmediateTexture::Resource white_texture_ = {};
  std::vector<VulkanImmediateTexture*> textures_;
  struct PendingTextureUpload {
    // Null for internal resources such as the white texture.
    VulkanImmediateTexture* texture;
    // VK_NULL_HANDLE if need to clear rather than to copy.
    VkBuffer buffer;
    VkDeviceMemory buffer_memory;
    VkImage image;
    uint32_t width;
    uint32_t height;
  };
  std::vector<PendingTextureUpload> texture_uploads_pending_;
  struct SubmittedTextureUploadBuffer {
    VkBuffer buffer;
    VkDeviceMemory buffer_memory;
    uint64_t submission_index;
  };
  std::deque<SubmittedTextureUploadBuffer> texture_upload_buffers_submitted_;
  // Resource and last usage submission pairs.
  std::vector<std::pair<VulkanImmediateTexture::Resource, uint64_t>>
      textures_deleted_;

  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;

  std::unique_ptr<VulkanUploadBufferPool> vertex_buffer_pool_;

  VkFormat pipeline_framebuffer_format_ = VK_FORMAT_UNDEFINED;
  VkPipeline pipeline_triangle_ = VK_NULL_HANDLE;
  VkPipeline pipeline_line_ = VK_NULL_HANDLE;

  VkCommandBuffer current_command_buffer_ = VK_NULL_HANDLE;
  VkExtent2D current_render_target_extent_;
  VkRect2D current_scissor_;
  VkPipeline current_pipeline_;
  uint32_t current_texture_descriptor_index_;
  bool batch_open_ = false;
  bool batch_has_index_buffer_;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_IMMEDIATE_DRAWER_H_
