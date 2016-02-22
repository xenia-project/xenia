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
#include "xenia/gpu/vulkan/vulkan_gpu_flags.h"

namespace xe {
namespace gpu {
namespace vulkan {

using xe::ui::vulkan::CheckResult;

constexpr uint32_t kMaxTextureSamplers = 32;

TextureCache::TextureCache(RegisterFile* register_file,
                           ui::vulkan::VulkanDevice* device)
    : register_file_(register_file), device_(*device) {
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
  auto err = vkCreateDescriptorPool(device_, &descriptor_pool_info, nullptr,
                                    &descriptor_pool_);
  CheckResult(err, "vkCreateDescriptorPool");

  // Create the descriptor set layout used for rendering.
  // We always have the same number of samplers but only some are used.
  VkDescriptorSetLayoutBinding bindings[2];
  auto& sampler_binding = bindings[0];
  sampler_binding.binding = 0;
  sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
  sampler_binding.descriptorCount = kMaxTextureSamplers;
  sampler_binding.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  sampler_binding.pImmutableSamplers = nullptr;
  auto& texture_binding = bindings[1];
  texture_binding.binding = 1;
  texture_binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  texture_binding.descriptorCount = kMaxTextureSamplers;
  texture_binding.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  texture_binding.pImmutableSamplers = nullptr;
  VkDescriptorSetLayoutCreateInfo descriptor_set_layout_info;
  descriptor_set_layout_info.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_set_layout_info.pNext = nullptr;
  descriptor_set_layout_info.flags = 0;
  descriptor_set_layout_info.bindingCount =
      static_cast<uint32_t>(xe::countof(bindings));
  descriptor_set_layout_info.pBindings = bindings;
  err = vkCreateDescriptorSetLayout(device_, &descriptor_set_layout_info,
                                    nullptr, &texture_descriptor_set_layout_);
  CheckResult(err, "vkCreateDescriptorSetLayout");
}

TextureCache::~TextureCache() {
  vkDestroyDescriptorSetLayout(device_, texture_descriptor_set_layout_,
                               nullptr);
  vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
}

void TextureCache::ClearCache() {
  // TODO(benvanik): caching.
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
