/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/texture_cache.h"

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/sampler_info.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/vulkan/vulkan_gpu_flags.h"

namespace xe {
namespace gpu {
namespace vulkan {

using xe::ui::vulkan::CheckResult;

constexpr uint32_t kMaxTextureSamplers = 32;

TextureCache::TextureCache(RegisterFile* register_file,
                           TraceWriter* trace_writer,
                           ui::vulkan::VulkanDevice* device)
    : register_file_(register_file),
      trace_writer_(trace_writer),
      device_(device) {
  // Descriptor pool used for all of our cached descriptors.
  VkDescriptorPoolCreateInfo descriptor_pool_info;
  descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptor_pool_info.pNext = nullptr;
  descriptor_pool_info.flags =
      VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  descriptor_pool_info.maxSets = 256;
  VkDescriptorPoolSize pool_sizes[2];
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_SAMPLER;
  pool_sizes[0].descriptorCount = 32;
  pool_sizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  pool_sizes[1].descriptorCount = 32;
  descriptor_pool_info.poolSizeCount = 2;
  descriptor_pool_info.pPoolSizes = pool_sizes;
  auto err = vkCreateDescriptorPool(*device_, &descriptor_pool_info, nullptr,
                                    &descriptor_pool_);
  CheckResult(err, "vkCreateDescriptorPool");

  // Create the descriptor set layout used for rendering.
  // We always have the same number of samplers but only some are used.
  VkDescriptorSetLayoutBinding bindings[5];
  auto& sampler_binding = bindings[0];
  sampler_binding.binding = 0;
  sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  sampler_binding.descriptorCount = kMaxTextureSamplers;
  sampler_binding.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  sampler_binding.pImmutableSamplers = nullptr;
  for (int i = 0; i < 4; ++i) {
    auto& texture_binding = bindings[1 + i];
    texture_binding.binding = 1 + i;
    texture_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    texture_binding.descriptorCount = kMaxTextureSamplers;
    texture_binding.stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    texture_binding.pImmutableSamplers = nullptr;
  }
  VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info;
  descriptor_set_layout_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_set_layout_info.pNext = nullptr;
  descriptor_set_layout_info.flags = 0;
  descriptor_set_layout_info.bindingCount =
      static_cast<uint32_t>(xe::countof(bindings));
  descriptor_set_layout_info.pBindings = bindings;
  err = vkCreateDescriptorSetLayout(*device_, &descriptor_set_layout_info,
                                    nullptr, &texture_descriptor_set_layout_);
  CheckResult(err, "vkCreateDescriptorSetLayout");

  SetupGridImages();
}

TextureCache::~TextureCache() {
  vkDestroyImageView(*device_, grid_image_2d_view_, nullptr);
  vkDestroyImage(*device_, grid_image_2d_, nullptr);
  vkFreeMemory(*device_, grid_image_2d_memory_, nullptr);

  vkDestroyDescriptorSetLayout(*device_, texture_descriptor_set_layout_,
                               nullptr);
  vkDestroyDescriptorPool(*device_, descriptor_pool_, nullptr);
}

void TextureCache::SetupGridImages() {
  VkImageCreateInfo image_info;
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.pNext = nullptr;
  image_info.flags = 0;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  image_info.extent = {8, 8, 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_LINEAR;
  image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.queueFamilyIndexCount = 0;
  image_info.pQueueFamilyIndices = nullptr;
  image_info.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  auto err = vkCreateImage(*device_, &image_info, nullptr, &grid_image_2d_);
  CheckResult(err, "vkCreateImage");

  VkMemoryRequirements memory_requirements;
  vkGetImageMemoryRequirements(*device_, grid_image_2d_, &memory_requirements);
  grid_image_2d_memory_ = device_->AllocateMemory(
      memory_requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  err = vkBindImageMemory(*device_, grid_image_2d_, grid_image_2d_memory_, 0);
  CheckResult(err, "vkBindImageMemory");

  VkImageViewCreateInfo view_info;
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.pNext = nullptr;
  view_info.flags = 0;
  view_info.image = grid_image_2d_;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  view_info.components = {
      VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B,
      VK_COMPONENT_SWIZZLE_A,
  };
  view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  err = vkCreateImageView(*device_, &view_info, nullptr, &grid_image_2d_view_);
  CheckResult(err, "vkCreateImageView");

  VkImageSubresource subresource;
  subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  subresource.mipLevel = 0;
  subresource.arrayLayer = 0;
  VkSubresourceLayout layout;
  vkGetImageSubresourceLayout(*device_, grid_image_2d_, &subresource, &layout);

  void* gpu_data = nullptr;
  err = vkMapMemory(*device_, grid_image_2d_memory_, 0, layout.size, 0,
                    &gpu_data);
  CheckResult(err, "vkMapMemory");

  uint32_t grid_pixels[8 * 8];
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      grid_pixels[y * 8 + x] =
          ((y % 2 == 0) ^ (x % 2 != 0)) ? 0xFFFFFFFF : 0xFF0000FF;
    }
  }
  std::memcpy(gpu_data, grid_pixels, sizeof(grid_pixels));

  vkUnmapMemory(*device_, grid_image_2d_memory_);
}

VkDescriptorSet TextureCache::PrepareTextureSet(
    VkCommandBuffer command_buffer,
    const std::vector<Shader::TextureBinding>& vertex_bindings,
    const std::vector<Shader::TextureBinding>& pixel_bindings) {
  // Clear state.
  auto update_set_info = &update_set_info_;
  update_set_info->has_setup_fetch_mask = 0;
  update_set_info->image_1d_write_count = 0;
  update_set_info->image_2d_write_count = 0;
  update_set_info->image_3d_write_count = 0;
  update_set_info->image_cube_write_count = 0;

  std::memset(update_set_info, 0, sizeof(update_set_info_));

  // Process vertex and pixel shader bindings.
  // This does things lazily and de-dupes fetch constants reused in both
  // shaders.
  bool any_failed = false;
  any_failed =
      !SetupTextureBindings(update_set_info, vertex_bindings) || any_failed;
  any_failed =
      !SetupTextureBindings(update_set_info, pixel_bindings) || any_failed;
  if (any_failed) {
    XELOGW("Failed to setup one or more texture bindings");
    // TODO(benvanik): actually bail out here?
  }

  // TODO(benvanik): reuse.
  VkDescriptorSet descriptor_set = nullptr;
  VkDescriptorSetAllocateInfo set_alloc_info;
  set_alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  set_alloc_info.pNext = nullptr;
  set_alloc_info.descriptorPool = descriptor_pool_;
  set_alloc_info.descriptorSetCount = 1;
  set_alloc_info.pSetLayouts = &texture_descriptor_set_layout_;
  auto err =
      vkAllocateDescriptorSets(*device_, &set_alloc_info, &descriptor_set);
  CheckResult(err, "vkAllocateDescriptorSets");

  // Write all updated descriptors.
  // TODO(benvanik): optimize? split into multiple sets? set per type?
  VkWriteDescriptorSet descriptor_writes[4];
  std::memset(descriptor_writes, 0, sizeof(descriptor_writes));
  uint32_t descriptor_write_count = 0;
  if (update_set_info->sampler_write_count) {
    auto& sampler_write = descriptor_writes[descriptor_write_count++];
    sampler_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    sampler_write.pNext = nullptr;
    sampler_write.dstSet = descriptor_set;
    sampler_write.dstBinding = 0;
    sampler_write.dstArrayElement = 0;
    sampler_write.descriptorCount = update_set_info->sampler_write_count;
    sampler_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    sampler_write.pImageInfo = update_set_info->sampler_infos;
  }
  if (update_set_info->image_1d_write_count) {
    auto& image_write = descriptor_writes[descriptor_write_count++];
    image_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    image_write.pNext = nullptr;
    image_write.dstSet = descriptor_set;
    image_write.dstBinding = 1;
    image_write.dstArrayElement = 0;
    image_write.descriptorCount = update_set_info->image_1d_write_count;
    image_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    image_write.pImageInfo = update_set_info->image_1d_infos;
  }
  if (update_set_info->image_2d_write_count) {
    auto& image_write = descriptor_writes[descriptor_write_count++];
    image_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    image_write.pNext = nullptr;
    image_write.dstSet = descriptor_set;
    image_write.dstBinding = 2;
    image_write.dstArrayElement = 0;
    image_write.descriptorCount = update_set_info->image_2d_write_count;
    image_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    image_write.pImageInfo = update_set_info->image_2d_infos;
  }
  if (update_set_info->image_3d_write_count) {
    auto& image_write = descriptor_writes[descriptor_write_count++];
    image_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    image_write.pNext = nullptr;
    image_write.dstSet = descriptor_set;
    image_write.dstBinding = 3;
    image_write.dstArrayElement = 0;
    image_write.descriptorCount = update_set_info->image_3d_write_count;
    image_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    image_write.pImageInfo = update_set_info->image_3d_infos;
  }
  if (update_set_info->image_cube_write_count) {
    auto& image_write = descriptor_writes[descriptor_write_count++];
    image_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    image_write.pNext = nullptr;
    image_write.dstSet = descriptor_set;
    image_write.dstBinding = 4;
    image_write.dstArrayElement = 0;
    image_write.descriptorCount = update_set_info->image_cube_write_count;
    image_write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    image_write.pImageInfo = update_set_info->image_cube_infos;
  }
  if (descriptor_write_count) {
    vkUpdateDescriptorSets(*device_, descriptor_write_count, descriptor_writes,
                           0, nullptr);
  }

  return descriptor_set;
}

bool TextureCache::SetupTextureBindings(
    UpdateSetInfo* update_set_info,
    const std::vector<Shader::TextureBinding>& bindings) {
  bool any_failed = false;
  for (auto& binding : bindings) {
    uint32_t fetch_bit = 1 << binding.fetch_constant;
    if ((update_set_info->has_setup_fetch_mask & fetch_bit) == 0) {
      // Needs setup.
      any_failed = !SetupTextureBinding(update_set_info, binding) || any_failed;
      update_set_info->has_setup_fetch_mask |= fetch_bit;
    }
  }
  return !any_failed;
}

bool TextureCache::SetupTextureBinding(UpdateSetInfo* update_set_info,
                                       const Shader::TextureBinding& binding) {
  auto& regs = *register_file_;
  int r = XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0 + binding.fetch_constant * 6;
  auto group =
      reinterpret_cast<const xenos::xe_gpu_fetch_group_t*>(&regs.values[r]);
  auto& fetch = group->texture_fetch;

  // Disabled?
  // TODO(benvanik): reset sampler.
  if (!fetch.type) {
    return true;
  }
  assert_true(fetch.type == 0x2);

  TextureInfo texture_info;
  if (!TextureInfo::Prepare(fetch, &texture_info)) {
    XELOGE("Unable to parse texture fetcher info");
    return false;  // invalid texture used
  }
  SamplerInfo sampler_info;
  if (!SamplerInfo::Prepare(fetch, binding.fetch_instr, &sampler_info)) {
    XELOGE("Unable to parse sampler info");
    return false;  // invalid texture used
  }

  trace_writer_->WriteMemoryRead(texture_info.guest_address,
                                 texture_info.input_length);

  // TODO(benvanik): reuse.
  VkSamplerCreateInfo sampler_create_info;
  sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_create_info.pNext = nullptr;
  sampler_create_info.flags = 0;
  sampler_create_info.magFilter = VK_FILTER_NEAREST;
  sampler_create_info.minFilter = VK_FILTER_NEAREST;
  sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_create_info.mipLodBias = 0.0f;
  sampler_create_info.anisotropyEnable = VK_FALSE;
  sampler_create_info.maxAnisotropy = 1.0f;
  sampler_create_info.compareEnable = VK_FALSE;
  sampler_create_info.compareOp = VK_COMPARE_OP_ALWAYS;
  sampler_create_info.minLod = 0.0f;
  sampler_create_info.maxLod = 0.0f;
  sampler_create_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
  sampler_create_info.unnormalizedCoordinates = VK_FALSE;
  VkSampler sampler;
  auto err = vkCreateSampler(*device_, &sampler_create_info, nullptr, &sampler);
  CheckResult(err, "vkCreateSampler");

  auto& sampler_write =
      update_set_info->sampler_infos[update_set_info->sampler_write_count++];
  sampler_write.sampler = sampler;

  auto& image_write =
      update_set_info->image_2d_infos[update_set_info->image_2d_write_count++];
  image_write.imageView = grid_image_2d_view_;
  image_write.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

  return true;
}

void TextureCache::ClearCache() {
  // TODO(benvanik): caching.
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
