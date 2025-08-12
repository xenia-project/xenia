/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_provider.h"

#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/ui/vulkan/vulkan_immediate_drawer.h"
#include "xenia/ui/vulkan/vulkan_presenter.h"

DEFINE_bool(
    vulkan_validation, false,
    "Enable the Vulkan validation layer (VK_LAYER_KHRONOS_validation). "
    "Messages will be written to the Xenia log if 'vulkan_log_debug_messages' "
    "is enabled, or to the OS debug output otherwise.",
    "Vulkan");

DEFINE_int32(vulkan_device, -1,
             "Index of the preferred Vulkan physical device, or -1 to use any "
             "compatible device.",
             "Vulkan");

namespace xe {
namespace ui {
namespace vulkan {

std::unique_ptr<VulkanProvider> VulkanProvider::Create(
    const bool with_gpu_emulation, const bool with_presentation) {
  std::unique_ptr<VulkanProvider> provider(new VulkanProvider());

  provider->vulkan_instance_ =
      VulkanInstance::Create(with_presentation, cvars::vulkan_validation);
  if (!provider->vulkan_instance_) {
    return nullptr;
  }

  std::vector<VkPhysicalDevice> physical_devices;
  provider->vulkan_instance_->EnumeratePhysicalDevices(physical_devices);

  if (physical_devices.empty()) {
    XELOGW("No Vulkan physical devices available");
    return nullptr;
  }

  const VulkanInstance::Functions& ifn =
      provider->vulkan_instance_->functions();

  XELOGW(
      "Available Vulkan physical devices (use the 'vulkan_device' "
      "configuration variable to force a specific device):");
  for (size_t physical_device_index = 0;
       physical_device_index < physical_devices.size();
       ++physical_device_index) {
    VkPhysicalDeviceProperties physical_device_properties;
    ifn.vkGetPhysicalDeviceProperties(physical_devices[physical_device_index],
                                      &physical_device_properties);
    XELOGW("* {}: {}", physical_device_index,
           physical_device_properties.deviceName);
  }

  const int32_t preferred_physical_device_index = cvars::vulkan_device;
  if (preferred_physical_device_index >= 0 &&
      uint32_t(preferred_physical_device_index) < physical_devices.size()) {
    provider->vulkan_device_ = VulkanDevice::CreateIfSupported(
        provider->vulkan_instance_.get(),
        physical_devices[preferred_physical_device_index], with_gpu_emulation,
        with_presentation);
  }

  if (!provider->vulkan_device_) {
    for (const VkPhysicalDevice physical_device : physical_devices) {
      provider->vulkan_device_ = VulkanDevice::CreateIfSupported(
          provider->vulkan_instance_.get(), physical_device, with_gpu_emulation,
          with_presentation);
      if (provider->vulkan_device_) {
        break;
      }
    }

    if (!provider->vulkan_device_) {
      XELOGW(
          "Couldn't choose a compatible Vulkan physical device or initialize a "
          "Vulkan logical device");
      return nullptr;
    }
  }

  if (with_presentation) {
    provider->ui_samplers_ = UISamplers::Create(provider->vulkan_device_.get());
    if (!provider->ui_samplers_) {
      return nullptr;
    }
  }

  return provider;
}

std::unique_ptr<Presenter> VulkanProvider::CreatePresenter(
    Presenter::HostGpuLossCallback host_gpu_loss_callback) {
  return VulkanPresenter::Create(host_gpu_loss_callback, vulkan_device(),
                                 ui_samplers());
}

std::unique_ptr<ImmediateDrawer> VulkanProvider::CreateImmediateDrawer() {
  return VulkanImmediateDrawer::Create(vulkan_device(), ui_samplers());
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
