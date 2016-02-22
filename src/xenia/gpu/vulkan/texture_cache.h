/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_TEXTURE_CACHE_H_
#define XENIA_GPU_VULKAN_TEXTURE_CACHE_H_

#include "xenia/gpu/register_file.h"
#include "xenia/gpu/shader.h"
#include "xenia/gpu/trace_writer.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace gpu {
namespace vulkan {

//
class TextureCache {
 public:
  TextureCache(RegisterFile* register_file, TraceWriter* trace_writer,
               ui::vulkan::VulkanDevice* device);
  ~TextureCache();

  // Descriptor set layout containing all possible texture bindings.
  // The set contains one descriptor for each texture sampler [0-31].
  VkDescriptorSetLayout texture_descriptor_set_layout() const {
    return texture_descriptor_set_layout_;
  }

  // Prepares a descriptor set containing the samplers and images for all
  // bindings. The textures will be uploaded/converted/etc as needed.
  VkDescriptorSet PrepareTextureSet(
      VkCommandBuffer command_buffer,
      const std::vector<Shader::TextureBinding>& vertex_bindings,
      const std::vector<Shader::TextureBinding>& pixel_bindings);

  // TODO(benvanik): UploadTexture.
  // TODO(benvanik): Resolve.
  // TODO(benvanik): ReadTexture.

  // Clears all cached content.
  void ClearCache();

 private:
  struct UpdateSetInfo;

  void SetupGridImages();

  bool SetupTextureBindings(
      UpdateSetInfo* update_set_info,
      const std::vector<Shader::TextureBinding>& bindings);
  bool SetupTextureBinding(UpdateSetInfo* update_set_info,
                           const Shader::TextureBinding& binding);

  RegisterFile* register_file_ = nullptr;
  TraceWriter* trace_writer_ = nullptr;
  ui::vulkan::VulkanDevice* device_ = nullptr;

  VkDescriptorPool descriptor_pool_ = nullptr;
  VkDescriptorSetLayout texture_descriptor_set_layout_ = nullptr;

  VkDeviceMemory grid_image_2d_memory_ = nullptr;
  VkImage grid_image_2d_ = nullptr;
  VkImageView grid_image_2d_view_ = nullptr;

  struct UpdateSetInfo {
    // Bitmap of all 32 fetch constants and whether they have been setup yet.
    // This prevents duplication across the vertex and pixel shader.
    uint32_t has_setup_fetch_mask;
    uint32_t sampler_write_count = 0;
    VkDescriptorImageInfo sampler_infos[32];
    uint32_t image_1d_write_count = 0;
    VkDescriptorImageInfo image_1d_infos[32];
    uint32_t image_2d_write_count = 0;
    VkDescriptorImageInfo image_2d_infos[32];
    uint32_t image_3d_write_count = 0;
    VkDescriptorImageInfo image_3d_infos[32];
    uint32_t image_cube_write_count = 0;
    VkDescriptorImageInfo image_cube_infos[32];
  } update_set_info_;
};

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_VULKAN_TEXTURE_CACHE_H_
