/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_TEXTURE_CACHE_H_
#define XENIA_GPU_VULKAN_VULKAN_TEXTURE_CACHE_H_

#include <algorithm>
#include <list>
#include <unordered_map>
#include <unordered_set>

#include "xenia/base/mutex.h"
#include "xenia/gpu/register_file.h"
#include "xenia/gpu/sampler_info.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/texture_conversion.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/trace_writer.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/circular_buffer.h"
#include "xenia/ui/vulkan/fenced_pools.h"
#include "xenia/ui/vulkan/vulkan_mem_alloc.h"
#include "xenia/ui/vulkan/vulkan_provider.h"

namespace xe {
namespace gpu {
namespace vulkan {

//
class VulkanTextureCache {
 public:
  struct TextureView;

  // This represents an uploaded Vulkan texture.
  struct Texture {
    TextureInfo texture_info;
    std::vector<std::unique_ptr<TextureView>> views;

    VkFormat format;
    VkImage image;
    VkImageLayout image_layout;
    VmaAllocation alloc;
    VmaAllocationInfo alloc_info;
    VkFramebuffer framebuffer;  // Blit target frame buffer.
    VkImageUsageFlags usage_flags;

    bool is_watched;
    bool pending_invalidation;

    // Pointer to the latest usage fence.
    VkFence in_flight_fence;
  };

  struct TextureView {
    Texture* texture;
    VkImageView view;

    union {
      uint16_t swizzle;
      struct {
        // FIXME: This only applies on little-endian platforms!
        uint16_t swiz_x : 3;
        uint16_t swiz_y : 3;
        uint16_t swiz_z : 3;
        uint16_t swiz_w : 3;
        uint16_t : 4;
      };
    };
  };

  VulkanTextureCache(Memory* memory, RegisterFile* register_file,
                     TraceWriter* trace_writer,
                     ui::vulkan::VulkanProvider& provider);
  ~VulkanTextureCache();

  VkResult Initialize();
  void Shutdown();

  // Descriptor set layout containing all possible texture bindings.
  // The set contains one descriptor for each texture sampler [0-31].
  VkDescriptorSetLayout texture_descriptor_set_layout() const {
    return texture_descriptor_set_layout_;
  }

  // Prepares a descriptor set containing the samplers and images for all
  // bindings. The textures will be uploaded/converted/etc as needed.
  // Requires a fence to be provided that will be signaled when finished
  // using the returned descriptor set.
  VkDescriptorSet PrepareTextureSet(
      VkCommandBuffer setup_command_buffer, VkFence completion_fence,
      const std::vector<Shader::TextureBinding>& vertex_bindings,
      const std::vector<Shader::TextureBinding>& pixel_bindings);

  // TODO(benvanik): ReadTexture.

  Texture* Lookup(const TextureInfo& texture_info);

  // Looks for a texture either containing or matching these parameters.
  // Caller is responsible for checking if the texture returned is an exact
  // match or just contains the texture given by the parameters.
  // If offset_x and offset_y are not null, this may return a texture that
  // contains this address at an offset.
  Texture* LookupAddress(uint32_t guest_address, uint32_t width,
                         uint32_t height, xenos::TextureFormat format,
                         VkOffset2D* out_offset = nullptr);

  TextureView* DemandView(Texture* texture, uint16_t swizzle);

  // Demands a texture for the purpose of resolving from EDRAM. This either
  // creates a new texture or returns a previously created texture.
  Texture* DemandResolveTexture(const TextureInfo& texture_info);

  // Clears all cached content.
  void ClearCache();

  // Frees any unused resources
  void Scavenge();

 private:
  struct UpdateSetInfo;

  // Cached Vulkan sampler.
  struct Sampler {
    SamplerInfo sampler_info;
    VkSampler sampler;
  };

  struct WatchedTexture {
    Texture* texture;
    bool is_mip;
  };

  // Allocates a new texture and memory to back it on the GPU.
  Texture* AllocateTexture(const TextureInfo& texture_info,
                           VkFormatFeatureFlags required_flags =
                               VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
  bool FreeTexture(Texture* texture);

  void WatchTexture(Texture* texture);
  void TextureTouched(Texture* texture);
  std::pair<uint32_t, uint32_t> MemoryInvalidationCallback(
      uint32_t physical_address_start, uint32_t length, bool exact_range);
  static std::pair<uint32_t, uint32_t> MemoryInvalidationCallbackThunk(
      void* context_ptr, uint32_t physical_address_start, uint32_t length,
      bool exact_range);

  // Demands a texture. If command_buffer is null and the texture hasn't been
  // uploaded to graphics memory already, we will return null and bail.
  Texture* Demand(const TextureInfo& texture_info,
                  VkCommandBuffer command_buffer = nullptr,
                  VkFence completion_fence = nullptr);
  Sampler* Demand(const SamplerInfo& sampler_info);

  void FlushPendingCommands(VkCommandBuffer command_buffer,
                            VkFence completion_fence);

  bool ConvertTexture(uint8_t* dest, VkBufferImageCopy* copy_region,
                      uint32_t mip, const TextureInfo& src);

  static const FormatInfo* GetFormatInfo(xenos::TextureFormat format);
  static texture_conversion::CopyBlockCallback GetFormatCopyBlock(
      xenos::TextureFormat format);
  static TextureExtent GetMipExtent(const TextureInfo& src, uint32_t mip);
  static uint32_t ComputeMipStorage(const FormatInfo* format_info,
                                    uint32_t width, uint32_t height,
                                    uint32_t depth, uint32_t mip);
  static uint32_t ComputeMipStorage(const TextureInfo& src, uint32_t mip);
  static uint32_t ComputeTextureStorage(const TextureInfo& src);

  // Writes a texture back into guest memory. This call is (mostly) asynchronous
  // but the texture must not be flagged for destruction.
  void WritebackTexture(Texture* texture);

  // Queues commands to upload a texture from system memory, applying any
  // conversions necessary. This may flush the command buffer to the GPU if we
  // run out of staging memory.
  bool UploadTexture(VkCommandBuffer command_buffer, VkFence completion_fence,
                     Texture* dest, const TextureInfo& src);

  void HashTextureBindings(XXH3_state_t* hash_state, uint32_t& fetch_mask,
                           const std::vector<Shader::TextureBinding>& bindings);
  bool SetupTextureBindings(
      VkCommandBuffer command_buffer, VkFence completion_fence,
      UpdateSetInfo* update_set_info,
      const std::vector<Shader::TextureBinding>& bindings);
  bool SetupTextureBinding(VkCommandBuffer command_buffer,
                           VkFence completion_fence,
                           UpdateSetInfo* update_set_info,
                           const Shader::TextureBinding& binding);

  // Removes invalidated textures from the cache, queues them for delete.
  void RemoveInvalidatedTextures();

  Memory* memory_ = nullptr;

  RegisterFile* register_file_ = nullptr;
  TraceWriter* trace_writer_ = nullptr;
  ui::vulkan::VulkanProvider& provider_;

  std::unique_ptr<xe::ui::vulkan::CommandBufferPool> wb_command_pool_ = nullptr;
  std::unique_ptr<xe::ui::vulkan::DescriptorPool> descriptor_pool_ = nullptr;
  std::unordered_map<uint64_t, VkDescriptorSet> texture_sets_;
  VkDescriptorSetLayout texture_descriptor_set_layout_ = nullptr;

  VmaAllocator mem_allocator_ = nullptr;

  ui::vulkan::CircularBuffer staging_buffer_;
  ui::vulkan::CircularBuffer wb_staging_buffer_;
  std::unordered_map<uint64_t, Texture*> textures_;
  std::unordered_map<uint64_t, Sampler*> samplers_;
  std::list<Texture*> pending_delete_textures_;

  void* memory_invalidation_callback_handle_ = nullptr;

  xe::global_critical_region global_critical_region_;
  std::list<WatchedTexture> watched_textures_;
  std::unordered_set<Texture*>* invalidated_textures_;
  std::unordered_set<Texture*> invalidated_textures_sets_[2];

  struct UpdateSetInfo {
    // Bitmap of all 32 fetch constants and whether they have been setup yet.
    // This prevents duplication across the vertex and pixel shader.
    uint32_t has_setup_fetch_mask;
    uint32_t image_write_count = 0;
    VkWriteDescriptorSet image_writes[32];
    VkDescriptorImageInfo image_infos[32];
  } update_set_info_;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_VULKAN_TEXTURE_CACHE_H_
