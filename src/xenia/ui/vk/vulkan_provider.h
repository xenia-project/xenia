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

#include <cstdint>
#include <memory>

#include "xenia/base/platform.h"
#include "xenia/ui/graphics_provider.h"

#if XE_PLATFORM_WIN32
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#elif XE_PLATFORM_LINUX
#include "xenia/ui/window_gtk.h"
#if defined(GDK_WINDOWING_X11) && !defined(VK_USE_PLATFORM_XCB_KHR)
#define VK_USE_PLATFORM_XCB_KHR 1
#endif
#endif
#include "third_party/volk/volk.h"

#define XELOGVK XELOGI

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

  VkInstance GetInstance() const { return instance_; }
  VkPhysicalDevice GetPhysicalDevice() const { return physical_device_; }
  const VkPhysicalDeviceProperties& GetPhysicalDeviceProperties() const {
    return physical_device_properties_;
  }
  const VkPhysicalDeviceFeatures& GetPhysicalDeviceFeatures() const {
    return physical_device_features_;
  }
  const VkPhysicalDeviceMemoryProperties& GetPhysicalDeviceMemoryProperties()
      const {
    return physical_device_memory_properties_;
  }
  VkDevice GetDevice() const { return device_; }
  uint32_t GetGraphicsQueueFamily() const { return graphics_queue_family_; }
  VkQueue GetGraphicsQueue() const { return graphics_queue_; }

  uint32_t FindMemoryType(uint32_t memory_type_bits_requirement,
                          VkMemoryPropertyFlags required_properties) const;

 private:
  explicit VulkanProvider(Window* main_window);

  bool Initialize();

  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkPhysicalDeviceProperties physical_device_properties_;
  VkPhysicalDeviceFeatures physical_device_features_;
  VkPhysicalDeviceMemoryProperties physical_device_memory_properties_;
  VkDevice device_ = VK_NULL_HANDLE;
  uint32_t graphics_queue_family_ = UINT32_MAX;
  VkQueue graphics_queue_ = VK_NULL_HANDLE;
};

}  // namespace vk
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VK_VULKAN_PROVIDER_H_
