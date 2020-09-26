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
#include <unordered_set>
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
    VulkanImmediateTexture(uint32_t width, uint32_t height,
                           VulkanImmediateDrawer* immediate_drawer,
                           uintptr_t immediate_drawer_handle)
        : ImmediateTexture(width, height), immediate_drawer_(immediate_drawer) {
      handle = immediate_drawer_handle;
    }
    ~VulkanImmediateTexture() {
      if (immediate_drawer_) {
        immediate_drawer_->HandleImmediateTextureDestroyed(handle);
      }
    }
    void DetachFromImmediateDrawer() {
      immediate_drawer_ = nullptr;
      handle = 0;
    }

   private:
    VulkanImmediateDrawer* immediate_drawer_;
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

  // Tracked separately from VulkanImmediateTexture because copying may take
  // additional references.
  struct Texture {
    // Null for the white texture, reference held by the drawer itself instead
    // of immediate textures.
    VulkanImmediateTexture* immediate_texture;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView image_view;
    uint32_t descriptor_index;
    uint32_t reference_count;
  };

  bool EnsurePipelinesCreated();

  // Allocates a combined image sampler in a pool and returns its index, or
  // UINT32_MAX in case of failure.
  uint32_t AllocateTextureDescriptor();
  VkDescriptorSet GetTextureDescriptor(uint32_t descriptor_index) const;
  void FreeTextureDescriptor(uint32_t descriptor_index);

  // Returns SIZE_MAX in case of failure. The created texture will have a
  // reference count of 1 plus references needed for uploading, but will not be
  // attached to a VulkanImmediateTexture (will return the reference to the
  // caller, in short). If data is null, a (1, 1, 1, 1) image will be created,
  // which can be used as a replacement when drawing without a real texture.
  size_t CreateVulkanTexture(uint32_t width, uint32_t height,
                             ImmediateTextureFilter filter, bool is_repeated,
                             const uint8_t* data);
  void ReleaseTexture(size_t index);
  uintptr_t GetTextureHandleForIndex(size_t index) const {
    return index != white_texture_index_ ? uintptr_t(index + 1) : 0;
  }
  size_t GetTextureIndexForHandle(uintptr_t handle) const {
    // 0 is a special value for no texture.
    return handle ? size_t(handle - 1) : white_texture_index_;
  }
  // For calling from VulkanImmediateTexture.
  void HandleImmediateTextureDestroyed(uintptr_t handle) {
    size_t index = GetTextureIndexForHandle(handle);
    if (index == white_texture_index_) {
      return;
    }
    textures_[index].immediate_texture = nullptr;
    ReleaseTexture(index);
  }

  VulkanContext& context_;

  // Combined image sampler pools for textures.
  VkDescriptorSetLayout texture_descriptor_set_layout_;
  std::vector<TextureDescriptorPool*> texture_descriptor_pools_;
  TextureDescriptorPool* texture_descriptor_pool_unallocated_first_ = nullptr;
  TextureDescriptorPool* texture_descriptor_pool_recycled_first_ = nullptr;

  std::vector<Texture> textures_;
  std::vector<size_t> textures_free_;
  struct PendingTextureUpload {
    size_t texture_index;
    uint32_t width;
    uint32_t height;
    // VK_NULL_HANDLE if need to clear rather than to copy.
    VkBuffer buffer;
    VkDeviceMemory buffer_memory;
  };
  std::vector<PendingTextureUpload> texture_uploads_pending_;
  struct SubmittedTextureUpload {
    size_t texture_index;
    // VK_NULL_HANDLE if cleared rather than copied.
    VkBuffer buffer;
    VkDeviceMemory buffer_memory;
    uint64_t submission_index;
  };
  std::deque<SubmittedTextureUpload> texture_uploads_submitted_;
  size_t white_texture_index_;

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
