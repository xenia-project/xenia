/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VK_VULKAN_PROVIDER_H_
#define XENIA_UI_VK_VULKAN_PROVIDER_H_

#include <memory>

#include "third_party/volk/volk.h"

#define XELOGVK XELOGI

#include "xenia/ui/graphics_provider.h"

namespace xe {
namespace ui {
namespace vk {

class VulkanImmediateDrawer;

class VulkanProvider : public GraphicsProvider {
 public:
  ~VulkanProvider() override;

  static std::unique_ptr<VulkanProvider> Create(Window* main_window);

  std::unique_ptr<GraphicsContext> CreateContext(
      Window* target_window) override;
  std::unique_ptr<GraphicsContext> CreateOffscreenContext() override;

  const VkPhysicalDeviceFeatures& GetPhysicalDeviceFeatures() const {
    return physical_device_features_;
  }
  VkDevice GetDevice() const { return device_; }
  VkQueue GetGraphicsQueue() const { return graphics_queue_; }

 private:
  explicit VulkanProvider(Window* main_window);

  bool Initialize();

  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkPhysicalDeviceFeatures physical_device_features_;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue graphics_queue_ = VK_NULL_HANDLE;
};

}  // namespace vk
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VK_VULKAN_PROVIDER_H_
