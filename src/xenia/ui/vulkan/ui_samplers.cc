/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/ui_samplers.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace vulkan {

std::unique_ptr<UISamplers> UISamplers::Create(
    const VulkanDevice* const vulkan_device) {
  assert_not_null(vulkan_device);

  std::unique_ptr<UISamplers> ui_samplers(new UISamplers(vulkan_device));

  const VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  VkSamplerCreateInfo sampler_create_info = {
      VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};

  for (int sampler_index = 0; sampler_index < kSamplerCount; ++sampler_index) {
    if (sampler_index == kSamplerIndexLinearRepeat ||
        sampler_index == kSamplerIndexLinearClampToEdge) {
      sampler_create_info.magFilter = VK_FILTER_LINEAR;
      sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    } else {
      sampler_create_info.magFilter = VK_FILTER_NEAREST;
      sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }
    sampler_create_info.minFilter = sampler_create_info.magFilter;

    if (sampler_index == kSamplerIndexNearestClampToEdge ||
        sampler_index == kSamplerIndexLinearClampToEdge) {
      sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    } else {
      sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
    sampler_create_info.addressModeV = sampler_create_info.addressModeU;
    sampler_create_info.addressModeW = sampler_create_info.addressModeU;

    const VkResult sampler_create_result =
        dfn.vkCreateSampler(device, &sampler_create_info, nullptr,
                            &ui_samplers->samplers_[sampler_index]);
    if (sampler_create_result != VK_SUCCESS) {
      XELOGE(
          "Failed to create the Vulkan UI sampler with filter {}, addressing "
          "mode {}: {}",
          vk::to_string(vk::Filter(sampler_create_info.magFilter)),
          vk::to_string(
              vk::SamplerAddressMode(sampler_create_info.addressModeU)),
          vk::to_string(vk::Result(sampler_create_result)));
      return nullptr;
    }
  }

  return ui_samplers;
}

UISamplers::~UISamplers() {
  for (const VkSampler sampler : samplers_) {
    if (sampler == VK_NULL_HANDLE) {
      continue;
    }
    vulkan_device_->functions().vkDestroySampler(vulkan_device_->device(),
                                                 sampler, nullptr);
  }
}

UISamplers::UISamplers(const VulkanDevice* vulkan_device)
    : vulkan_device_(vulkan_device) {
  assert_not_null(vulkan_device);

  samplers_.fill(VK_NULL_HANDLE);
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
