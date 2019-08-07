/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vk/vulkan_provider.h"

#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/ui/vk/vulkan_context.h"
#include "xenia/ui/vk/vulkan_util.h"

DEFINE_bool(vk_validation, false, "Enable Vulkan validation layers.", "Vulkan");
DEFINE_int32(vk_device, -1,
             "Index of the Vulkan physical device to use. -1 to use any "
             "compatible.",
             "Vulkan");

namespace xe {
namespace ui {
namespace vk {

std::unique_ptr<VulkanProvider> VulkanProvider::Create(Window* main_window) {
  std::unique_ptr<VulkanProvider> provider(new VulkanProvider(main_window));
  if (!provider->Initialize()) {
    xe::FatalError(
        "Unable to initialize Vulkan 1.1 graphics subsystem.\n"
        "\n"
        "Ensure you have the latest drivers for your GPU and that it supports "
        "Vulkan, and install the latest Vulkan runtime from "
        "https://vulkan.lunarg.com/sdk/home.\n"
        "\n"
        "See https://xenia.jp/faq/ for more information and a list of "
        "supported GPUs.");
    return nullptr;
  }
  return provider;
}

VulkanProvider::VulkanProvider(Window* main_window)
    : GraphicsProvider(main_window) {}

VulkanProvider::~VulkanProvider() {
  if (device_ != VK_NULL_HANDLE) {
    vkDestroyDevice(device_, nullptr);
  }
  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, nullptr);
  }
}

bool VulkanProvider::Initialize() {
  if (volkInitialize() != VK_SUCCESS) {
    XELOGE("Failed to initialize the Vulkan loader volk.");
    return false;
  }

  const uint32_t api_version = VK_MAKE_VERSION(1, 1, 0);

  // Create the instance.
  VkApplicationInfo application_info;
  application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application_info.pNext = nullptr;
  application_info.pApplicationName = "Xenia";
  application_info.applicationVersion = 1;
  application_info.pEngineName = "Xenia";
  application_info.engineVersion = 1;
  application_info.apiVersion = api_version;
  const char* const validation_layers[] = {
      "VK_LAYER_LUNARG_standard_validation",
  };
  const char* const instance_extensions[] = {
    "VK_KHR_surface",
#if XE_PLATFORM_WIN32
    "VK_KHR_win32_surface",
#endif
  };
  VkInstanceCreateInfo instance_create_info;
  instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_create_info.pNext = nullptr;
  instance_create_info.flags = 0;
  instance_create_info.pApplicationInfo = &application_info;
  if (cvars::vk_validation) {
    instance_create_info.enabledLayerCount =
        uint32_t(xe::countof(validation_layers));
    instance_create_info.ppEnabledLayerNames = validation_layers;
  } else {
    instance_create_info.enabledLayerCount = 0;
    instance_create_info.ppEnabledLayerNames = nullptr;
  }
  instance_create_info.enabledExtensionCount =
      uint32_t(xe::countof(instance_extensions));
  instance_create_info.ppEnabledExtensionNames = instance_extensions;
  if (vkCreateInstance(&instance_create_info, nullptr, &instance_) !=
      VK_SUCCESS) {
    XELOGE("Failed to create a Vulkan instance.");
    return false;
  }
  volkLoadInstance(instance_);

  // Get a supported physical device.
  physical_device_ = nullptr;
  std::vector<VkPhysicalDevice> physical_devices;
  uint32_t physical_device_count;
  if (vkEnumeratePhysicalDevices(instance_, &physical_device_count, nullptr) !=
      VK_SUCCESS) {
    XELOGE("Failed to get Vulkan physical device count.");
    return false;
  }
  physical_devices.resize(physical_device_count);
  if (vkEnumeratePhysicalDevices(instance_, &physical_device_count,
                                 physical_devices.data()) != VK_SUCCESS) {
    XELOGE("Failed to get Vulkan physical devices.");
    return false;
  }
  physical_devices.resize(physical_device_count);
  uint32_t physical_device_index, physical_device_index_end;
  if (cvars::vk_device >= 0) {
    physical_device_index = uint32_t(cvars::vk_device);
    physical_device_index_end =
        std::min(physical_device_index + 1, physical_device_count);
  } else {
    physical_device_index = 0;
    physical_device_index_end = physical_device_count;
  }
  VkPhysicalDeviceFeatures physical_device_features;
  std::vector<VkQueueFamilyProperties> queue_families;
  uint32_t queue_family = UINT32_MAX;
  bool sparse_residency_buffer = false;
  for (; physical_device_index < physical_device_index_end;
       ++physical_device_index) {
    VkPhysicalDevice physical_device = physical_devices[physical_device_index];
    vkGetPhysicalDeviceFeatures(physical_device, &physical_device_features);
    sparse_residency_buffer = physical_device_features.sparseBinding &&
                              physical_device_features.sparseResidencyBuffer;
    // Get a queue supporting graphics, compute and transfer, and if available,
    // also sparse memory management.
    queue_family = UINT32_MAX;
    uint32_t queue_family_count;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device,
                                             &queue_family_count, nullptr);
    queue_families.resize(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(
        physical_device, &queue_family_count, queue_families.data());
    const uint32_t queue_flags_required =
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    for (uint32_t i = 0; i < queue_family_count; ++i) {
      const VkQueueFamilyProperties& queue_family_properties =
          queue_families[i];
      // Arbitrary copying done when loading textures.
      if (queue_family_properties.minImageTransferGranularity.width > 1 ||
          queue_family_properties.minImageTransferGranularity.height > 1 ||
          queue_family_properties.minImageTransferGranularity.depth > 1) {
        continue;
      }
      if ((queue_family_properties.queueFlags & queue_flags_required) !=
          queue_flags_required) {
        continue;
      }
      queue_family = i;
      if (!sparse_residency_buffer ||
          (queue_family_properties.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT)) {
        // Found a fully compatible queue family, stop searching for a family
        // that support both graphics/compute/transfer and sparse binding.
        break;
      }
    }
    if (queue_family == UINT32_MAX) {
      continue;
    }
    if (!(queue_families[queue_family].queueFlags &
          VK_QUEUE_SPARSE_BINDING_BIT)) {
      sparse_residency_buffer = false;
    }
    physical_device_ = physical_device;
    break;
  }
  if (physical_device_ == VK_NULL_HANDLE) {
    XELOGE("Failed to get a supported Vulkan physical device.");
    return false;
  }
  supports_sparse_residency_buffer_ = sparse_residency_buffer;
  supports_texture_compression_bc_ =
      physical_device_features.textureCompressionBC != VK_FALSE;
  // TODO(Triang3l): Check if VK_EXT_fragment_shader_interlock and
  // fragmentShaderSampleInterlock are supported.

  // Log physical device properties.
  VkPhysicalDeviceProperties physical_device_properties;
  vkGetPhysicalDeviceProperties(physical_device_, &physical_device_properties);
  XELOGVK("Vulkan physical device: %s (vendor %.4X, device %.4X)",
          physical_device_properties.deviceName,
          physical_device_properties.vendorID,
          physical_device_properties.deviceID);
  XELOGVK("* Sparse buffer residency: %s",
          supports_sparse_residency_buffer_ ? "yes" : "no");
  XELOGVK("* BC texture compression: %s",
          supports_texture_compression_bc_ ? "yes" : "no");

  return true;
}

std::unique_ptr<GraphicsContext> VulkanProvider::CreateContext(
    Window* target_window) {
  auto new_context =
      std::unique_ptr<VulkanContext>(new VulkanContext(this, target_window));
  if (!new_context->Initialize()) {
    return nullptr;
  }
  return std::unique_ptr<GraphicsContext>(new_context.release());
}

std::unique_ptr<GraphicsContext> VulkanProvider::CreateOffscreenContext() {
  auto new_context =
      std::unique_ptr<VulkanContext>(new VulkanContext(this, nullptr));
  if (!new_context->Initialize()) {
    return nullptr;
  }
  return std::unique_ptr<GraphicsContext>(new_context.release());
}

}  // namespace vk
}  // namespace ui
}  // namespace xe
