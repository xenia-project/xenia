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
  VkDescriptorPoolSize pool_sizes[1];
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_sizes[0].descriptorCount = 256;
  descriptor_pool_info.poolSizeCount = 1;
  descriptor_pool_info.pPoolSizes = pool_sizes;
  auto err = vkCreateDescriptorPool(device_, &descriptor_pool_info, nullptr,
                                    &descriptor_pool_);
  CheckResult(err, "vkCreateDescriptorPool");

  // Create the descriptor set layout used for rendering.
  // We always have the same number of samplers but only some are used.
  VkDescriptorSetLayoutBinding texture_bindings[1];
  for (int i = 0; i < 1; ++i) {
    auto& texture_binding = texture_bindings[i];
    texture_binding.binding = 0;
    texture_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
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
      static_cast<uint32_t>(xe::countof(texture_bindings));
  descriptor_set_layout_info.pBindings = texture_bindings;
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
