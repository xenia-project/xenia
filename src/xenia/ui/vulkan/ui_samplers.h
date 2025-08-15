/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_UI_SAMPLERS_H_
#define XENIA_UI_VULKAN_VULKAN_UI_SAMPLERS_H_

#include <array>
#include <memory>

#include "xenia/ui/vulkan/vulkan_device.h"

namespace xe {
namespace ui {
namespace vulkan {

/// Samplers that can be used for presentation and UI drawing.
/// Because maxSamplerAllocationCount can be as low as 4000 (on Nvidia and Intel
/// GPUs primarily), no other samplers must be created for UI purposes.
/// The rest of the sampler allocation space on the device must be available to
/// GPU emulation.
class UISamplers {
 public:
  static std::unique_ptr<UISamplers> Create(const VulkanDevice* vulkan_device);

  UISamplers(const UISamplers&) = delete;
  UISamplers& operator=(const UISamplers&) = delete;
  UISamplers(UISamplers&&) = delete;
  UISamplers& operator=(UISamplers&&) = delete;

  ~UISamplers();

  enum SamplerIndex {
    kSamplerIndexNearestRepeat,
    kSamplerIndexNearestClampToEdge,
    kSamplerIndexLinearRepeat,
    kSamplerIndexLinearClampToEdge,

    kSamplerCount,
  };

  const std::array<VkSampler, kSamplerCount>& samplers() const {
    return samplers_;
  }

 private:
  explicit UISamplers(const VulkanDevice* vulkan_device);

  const VulkanDevice* vulkan_device_;

  std::array<VkSampler, kSamplerCount> samplers_;
};

}  // namespace vulkan
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VULKAN_VULKAN_UI_SAMPLERS_H_
