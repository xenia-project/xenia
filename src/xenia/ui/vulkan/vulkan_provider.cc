/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_provider.h"

#include <cstring>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/ui/vulkan/vulkan_context.h"

#if XE_PLATFORM_LINUX
#include <dlfcn.h>
#elif XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

DEFINE_int32(
    vulkan_device, -1,
    "Index of the physical device to use, or -1 for any compatible device.",
    "Vulkan");

namespace xe {
namespace ui {
namespace vulkan {

std::unique_ptr<VulkanProvider> VulkanProvider::Create(Window* main_window) {
  std::unique_ptr<VulkanProvider> provider(new VulkanProvider(main_window));
  if (!provider->Initialize()) {
    xe::FatalError(
        "Unable to initialize Vulkan graphics subsystem.\n"
        "\n"
        "Ensure that you have the latest drivers for your GPU and it supports "
        "Vulkan, and that you have the latest Vulkan runtime installed, which "
        "can be downloaded at https://vulkan.lunarg.com/sdk/home.\n"
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
    ifn_.destroyDevice(device_, nullptr);
  }
  if (instance_ != VK_NULL_HANDLE) {
    destroyInstance_(instance_, nullptr);
  }

#if XE_PLATFORM_LINUX
  if (library_) {
    dlclose(library_);
  }
#elif XE_PLATFORM_WIN32
  if (library_) {
    FreeLibrary(library_);
  }
#endif
}

bool VulkanProvider::Initialize() {
  // Load the library.
#if XE_PLATFORM_LINUX
#if XE_PLATFORM_ANDROID
  const char* libvulkan_name = "libvulkan.so";
#else
  const char* libvulkan_name = "libvulkan.so.1";
#endif
  // http://developer.download.nvidia.com/mobile/shield/assets/Vulkan/UsingtheVulkanAPI.pdf
  library_ = dlopen(libvulkan_name, RTLD_NOW | RTLD_LOCAL);
  if (!library_) {
    XELOGE("Failed to load {}", libvulkan_name);
    return false;
  }
  getInstanceProcAddr_ =
      PFN_vkGetInstanceProcAddr(dlsym(library_, "vkGetInstanceProcAddr"));
  destroyInstance_ =
      PFN_vkDestroyInstance(dlsym(library_, "vkDestroyInstance"));
  if (!getInstanceProcAddr_ || !destroyInstance_) {
    XELOGE("Failed to get vkGetInstanceProcAddr and vkDestroyInstance from {}",
           libvulkan_name);
    return false;
  }
#elif XE_PLATFORM_WIN32
  library_ = LoadLibraryA("vulkan-1.dll");
  if (!library_) {
    XELOGE("Failed to load vulkan-1.dll");
    return false;
  }
  getInstanceProcAddr_ = PFN_vkGetInstanceProcAddr(
      GetProcAddress(library_, "vkGetInstanceProcAddr"));
  destroyInstance_ =
      PFN_vkDestroyInstance(GetProcAddress(library_, "vkDestroyInstance"));
  if (!getInstanceProcAddr_ || !destroyInstance_) {
    XELOGE(
        "Failed to get vkGetInstanceProcAddr and vkDestroyInstance from "
        "vulkan-1.dll");
    return false;
  }
#else
#error No Vulkan library loading provided for the target platform.
#endif
  assert_not_null(getInstanceProcAddr_);
  assert_not_null(destroyInstance_);
  bool library_functions_loaded = true;
  library_functions_loaded &=
      (library_functions_.createInstance = PFN_vkCreateInstance(
           getInstanceProcAddr_(VK_NULL_HANDLE, "vkCreateInstance"))) !=
      nullptr;
  library_functions_loaded &=
      (library_functions_.enumerateInstanceExtensionProperties =
           PFN_vkEnumerateInstanceExtensionProperties(getInstanceProcAddr_(
               VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"))) !=
      nullptr;
  if (!library_functions_loaded) {
    XELOGE("Failed to get Vulkan library function pointers");
    return false;
  }
  library_functions_.enumerateInstanceVersion_1_1 =
      PFN_vkEnumerateInstanceVersion(
          getInstanceProcAddr_(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));

  // Get the API version.
  const uint32_t api_version_target = VK_MAKE_VERSION(1, 2, 148);
  static_assert(VK_HEADER_VERSION_COMPLETE >= api_version_target,
                "Vulkan header files must be up to date");
  if (!library_functions_.enumerateInstanceVersion_1_1 ||
      library_functions_.enumerateInstanceVersion_1_1(&api_version_) !=
          VK_SUCCESS) {
    api_version_ = VK_API_VERSION_1_0;
  }
  XELOGVK("Vulkan instance version {}.{}.{}", VK_VERSION_MAJOR(api_version_),
          VK_VERSION_MINOR(api_version_), VK_VERSION_PATCH(api_version_));

  // Create the instance.
  std::vector<const char*> instance_extensions_enabled;
  instance_extensions_enabled.push_back("VK_KHR_surface");
#if XE_PLATFORM_ANDROID
  instance_extensions_enabled.push_back("VK_KHR_android_surface");
#elif XE_PLATFORM_WIN32
  instance_extensions_enabled.push_back("VK_KHR_win32_surface");
#endif
  VkApplicationInfo application_info;
  application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application_info.pNext = nullptr;
  application_info.pApplicationName = "Xenia";
  application_info.applicationVersion = 1;
  application_info.pEngineName = nullptr;
  application_info.engineVersion = 0;
  // "apiVersion must be the highest version of Vulkan that the application is
  //  designed to use"
  // "Vulkan 1.0 implementations were required to return
  //  VK_ERROR_INCOMPATIBLE_DRIVER if apiVersion was larger than 1.0"
  application_info.apiVersion = api_version_ >= VK_MAKE_VERSION(1, 1, 0)
                                    ? api_version_target
                                    : api_version_;
  VkInstanceCreateInfo instance_create_info;
  instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_create_info.pNext = nullptr;
  instance_create_info.flags = 0;
  instance_create_info.pApplicationInfo = &application_info;
  // TODO(Triang3l): Enable the validation layer.
  instance_create_info.enabledLayerCount = 0;
  instance_create_info.ppEnabledLayerNames = nullptr;
  instance_create_info.enabledExtensionCount =
      uint32_t(instance_extensions_enabled.size());
  instance_create_info.ppEnabledExtensionNames =
      instance_extensions_enabled.data();
  if (library_functions_.createInstance(&instance_create_info, nullptr,
                                        &instance_) != VK_SUCCESS) {
    XELOGE("Failed to create a Vulkan instance with surface support");
    return false;
  }

  // Get instance functions.
  bool instance_functions_loaded = true;
  instance_functions_loaded &=
      (ifn_.createDevice = PFN_vkCreateDevice(
           getInstanceProcAddr_(instance_, "vkCreateDevice"))) != nullptr;
  instance_functions_loaded &=
      (ifn_.destroyDevice = PFN_vkDestroyDevice(
           getInstanceProcAddr_(instance_, "vkDestroyDevice"))) != nullptr;
  instance_functions_loaded &=
      (ifn_.enumerateDeviceExtensionProperties =
           PFN_vkEnumerateDeviceExtensionProperties(getInstanceProcAddr_(
               instance_, "vkEnumerateDeviceExtensionProperties"))) != nullptr;
  instance_functions_loaded &=
      (ifn_.enumeratePhysicalDevices = PFN_vkEnumeratePhysicalDevices(
           getInstanceProcAddr_(instance_, "vkEnumeratePhysicalDevices"))) !=
      nullptr;
  instance_functions_loaded &=
      (ifn_.getDeviceProcAddr = PFN_vkGetDeviceProcAddr(
           getInstanceProcAddr_(instance_, "vkGetDeviceProcAddr"))) != nullptr;
  instance_functions_loaded &=
      (ifn_.getPhysicalDeviceFeatures = PFN_vkGetPhysicalDeviceFeatures(
           getInstanceProcAddr_(instance_, "vkGetPhysicalDeviceFeatures"))) !=
      nullptr;
  instance_functions_loaded &=
      (ifn_.getPhysicalDeviceProperties = PFN_vkGetPhysicalDeviceProperties(
           getInstanceProcAddr_(instance_, "vkGetPhysicalDeviceProperties"))) !=
      nullptr;
  instance_functions_loaded &=
      (ifn_.getPhysicalDeviceQueueFamilyProperties =
           PFN_vkGetPhysicalDeviceQueueFamilyProperties(getInstanceProcAddr_(
               instance_, "vkGetPhysicalDeviceQueueFamilyProperties"))) !=
      nullptr;
  instance_functions_loaded &=
      (ifn_.getPhysicalDeviceSurfaceSupportKHR =
           PFN_vkGetPhysicalDeviceSurfaceSupportKHR(getInstanceProcAddr_(
               instance_, "vkGetPhysicalDeviceSurfaceSupportKHR"))) != nullptr;
#if XE_PLATFORM_ANDROID
  instance_functions_loaded &=
      (ifn_.createAndroidSurfaceKHR = PFN_vkCreateAndroidSurfaceKHR(
           getInstanceProcAddr_(instance_, "vkCreateAndroidSurfaceKHR"))) !=
      nullptr;
#elif XE_PLATFORM_WIN32
  instance_functions_loaded &=
      (ifn_.createWin32SurfaceKHR = PFN_vkCreateWin32SurfaceKHR(
           getInstanceProcAddr_(instance_, "vkCreateWin32SurfaceKHR"))) !=
      nullptr;
#endif
  if (!instance_functions_loaded) {
    XELOGE("Failed to get Vulkan instance function pointers");
    return false;
  }

  // Get the compatible physical device.
  std::vector<VkPhysicalDevice> physical_devices;
  for (;;) {
    uint32_t physical_device_count = uint32_t(physical_devices.size());
    bool physical_devices_was_empty = physical_devices.empty();
    VkResult physical_device_enumerate_result = ifn_.enumeratePhysicalDevices(
        instance_, &physical_device_count,
        physical_devices_was_empty ? nullptr : physical_devices.data());
    // If the original device count was 0 (first call), SUCCESS is returned, not
    // INCOMPLETE.
    if (physical_device_enumerate_result == VK_SUCCESS ||
        physical_device_enumerate_result == VK_INCOMPLETE) {
      physical_devices.resize(physical_device_count);
      if (physical_device_enumerate_result == VK_SUCCESS &&
          (!physical_devices_was_empty || !physical_device_count)) {
        break;
      }
    } else {
      XELOGE("Failed to enumerate Vulkan physical devices");
      return false;
    }
  }
  if (physical_devices.empty()) {
    XELOGE("No Vulkan physical devices are available");
    return false;
  }
  size_t physical_device_index_first, physical_device_index_last;
  if (cvars::vulkan_device >= 0) {
    physical_device_index_first = uint32_t(cvars::vulkan_device);
    physical_device_index_last = physical_device_index_first;
    if (physical_device_index_first >= physical_devices.size()) {
      XELOGE(
          "vulkan_device config variable is out of range, {} devices are "
          "available",
          physical_devices.size());
      return false;
    }
  } else {
    physical_device_index_first = 0;
    physical_device_index_last = physical_devices.size() - 1;
  }
  physical_device_ = VK_NULL_HANDLE;
  std::vector<VkQueueFamilyProperties> queue_families;
  uint32_t queue_family_sparse_binding = UINT32_MAX;
  std::vector<VkExtensionProperties> device_extension_properties;
  for (size_t i = physical_device_index_first; i <= physical_device_index_last;
       ++i) {
    VkPhysicalDevice physical_device_current = physical_devices[i];

    // Get physical device features and check if the needed ones are supported.
    ifn_.getPhysicalDeviceFeatures(physical_device_current, &device_features_);
    // TODO(Triang3l): Make geometry shaders optional by providing compute
    // shader fallback (though that would require vertex shader stores).
    if (!device_features_.geometryShader) {
      continue;
    }

    // Get the graphics and compute queue, and also a sparse binding queue
    // (preferably the same for the least latency between the two, as Xenia
    // submits sparse binding commands right before graphics commands anyway).
    uint32_t queue_family_count = 0;
    ifn_.getPhysicalDeviceQueueFamilyProperties(physical_device_current,
                                                &queue_family_count, nullptr);
    queue_families.resize(queue_family_count);
    ifn_.getPhysicalDeviceQueueFamilyProperties(
        physical_device_current, &queue_family_count, queue_families.data());
    assert_true(queue_family_count == queue_families.size());
    queue_family_graphics_compute_ = UINT32_MAX;
    queue_family_sparse_binding = UINT32_MAX;
    constexpr VkQueueFlags flags_graphics_compute =
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    constexpr VkQueueFlags flags_graphics_compute_sparse =
        flags_graphics_compute | VK_QUEUE_SPARSE_BINDING_BIT;
    for (uint32_t j = 0; j < queue_family_count; ++j) {
      VkQueueFlags queue_flags = queue_families[j].queueFlags;
      if (device_features_.sparseBinding) {
        // First, check if the queue family supports both graphics/compute and
        // sparse binding. This would be the best for Xenia.
        if ((queue_flags & flags_graphics_compute_sparse) ==
            flags_graphics_compute_sparse) {
          queue_family_graphics_compute_ = j;
          queue_family_sparse_binding = j;
          break;
        }
        // If not supporting both, for sparse binding, for now (until a queue
        // supporting all three is found), pick the first queue supporting it.
        if ((queue_flags & VK_QUEUE_SPARSE_BINDING_BIT) &&
            queue_family_sparse_binding == UINT32_MAX) {
          queue_family_sparse_binding = j;
        }
      }
      // If the device supports sparse binding, for now (until a queue
      // supporting all three is found), pick the first queue supporting
      // graphics/compute for graphics.
      // If it doesn't, just pick the first queue supporting graphics/compute.
      if ((queue_flags & flags_graphics_compute) == flags_graphics_compute &&
          queue_family_graphics_compute_ == UINT32_MAX) {
        queue_family_graphics_compute_ = j;
        if (!device_features_.sparseBinding) {
          break;
        }
      }
    }
    // FIXME(Triang3l): Here we're assuming that the graphics/compute queue
    // family supports presentation to the surface. It is probably true for most
    // target Vulkan implementations, however, there are no guarantees in the
    // specification.
    // To check if the queue supports presentation, the target surface must
    // exist at this point. However, the actual window that is created in
    // GraphicsContext, not in GraphicsProvider.
    // While we do have main_window here, it's not necessarily the window that
    // presentation will actually happen to. Also, while on Windows the HWND is
    // persistent, on Android, ANativeWindow is destroyed whenever the activity
    // goes to background, and the application may even be started in background
    // (programmatically, or using ADB, while the device is locked), thus the
    // window doesn't necessarily exist at this point.
    if (queue_family_graphics_compute_ == UINT32_MAX) {
      continue;
    }

    // Get the extensions, check if swapchain is supported.
    device_extension_properties.clear();
    VkResult device_extensions_enumerate_result;
    for (;;) {
      uint32_t device_extension_count =
          uint32_t(device_extension_properties.size());
      bool device_extensions_was_empty = device_extension_properties.empty();
      device_extensions_enumerate_result =
          ifn_.enumerateDeviceExtensionProperties(
              physical_device_current, nullptr, &device_extension_count,
              device_extensions_was_empty ? nullptr
                                          : device_extension_properties.data());
      // If the original extension count was 0 (first call), SUCCESS is
      // returned, not INCOMPLETE.
      if (device_extensions_enumerate_result == VK_SUCCESS ||
          device_extensions_enumerate_result == VK_INCOMPLETE) {
        device_extension_properties.resize(device_extension_count);
        if (device_extensions_enumerate_result == VK_SUCCESS &&
            (!device_extensions_was_empty || !device_extension_count)) {
          break;
        }
      } else {
        break;
      }
    }
    if (device_extensions_enumerate_result != VK_SUCCESS) {
      continue;
    }
    std::memset(&device_extensions_, 0, sizeof(device_extensions_));
    bool device_supports_swapchain = false;
    for (const VkExtensionProperties& device_extension :
         device_extension_properties) {
      const char* device_extension_name = device_extension.extensionName;
      if (!std::strcmp(device_extension_name,
                       "VK_EXT_fragment_shader_interlock")) {
        device_extensions_.ext_fragment_shader_interlock = true;
      } else if (!std::strcmp(device_extension_name, "VK_KHR_swapchain")) {
        device_supports_swapchain = true;
      }
    }
    if (!device_supports_swapchain) {
      continue;
    }

    physical_device_ = physical_device_current;
    break;
  }
  if (physical_device_ == VK_NULL_HANDLE) {
    XELOGE(
        "Failed to get a compatible Vulkan physical device with swapchain "
        "support");
    return false;
  }
  ifn_.getPhysicalDeviceProperties(physical_device_, &device_properties_);
  XELOGVK(
      "Vulkan device: {} (vendor {:04X}, device {:04X}, driver {:08X}, API "
      "{}.{}.{})",
      device_properties_.deviceName, device_properties_.vendorID,
      device_properties_.deviceID, device_properties_.driverVersion,
      VK_VERSION_MAJOR(device_properties_.apiVersion),
      VK_VERSION_MINOR(device_properties_.apiVersion),
      VK_VERSION_PATCH(device_properties_.apiVersion));
  // TODO(Triang3l): Report properties, features, extensions.

  // Create the device.
  float queue_priority_high = 1.0f;
  VkDeviceQueueCreateInfo queue_create_infos[2];
  queue_create_infos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_create_infos[0].pNext = nullptr;
  queue_create_infos[0].flags = 0;
  queue_create_infos[0].queueFamilyIndex = queue_family_graphics_compute_;
  queue_create_infos[0].queueCount = 1;
  queue_create_infos[0].pQueuePriorities = &queue_priority_high;
  bool separate_sparse_binding_queue =
      queue_family_sparse_binding != UINT32_MAX &&
      queue_family_sparse_binding != queue_family_graphics_compute_;
  if (separate_sparse_binding_queue) {
    queue_create_infos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_infos[1].pNext = nullptr;
    queue_create_infos[1].flags = 0;
    queue_create_infos[1].queueFamilyIndex = queue_family_sparse_binding;
    queue_create_infos[1].queueCount = 1;
    queue_create_infos[1].pQueuePriorities = &queue_priority_high;
  }
  std::vector<const char*> device_extensions_enabled;
  device_extensions_enabled.push_back("VK_KHR_swapchain");
  if (device_extensions_.ext_fragment_shader_interlock) {
    device_extensions_enabled.push_back("VK_EXT_fragment_shader_interlock");
  }
  VkDeviceCreateInfo device_create_info;
  device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_create_info.pNext = nullptr;
  device_create_info.flags = 0;
  device_create_info.queueCreateInfoCount =
      separate_sparse_binding_queue ? 2 : 1;
  device_create_info.pQueueCreateInfos = queue_create_infos;
  // TODO(Triang3l): Enable the validation layer.
  device_create_info.enabledLayerCount = 0;
  device_create_info.ppEnabledLayerNames = nullptr;
  device_create_info.enabledExtensionCount =
      uint32_t(device_extensions_enabled.size());
  device_create_info.ppEnabledExtensionNames = device_extensions_enabled.data();
  // TODO(Triang3l): Enable only needed features.
  device_create_info.pEnabledFeatures = &device_features_;
  if (ifn_.createDevice(physical_device_, &device_create_info, nullptr,
                        &device_) != VK_SUCCESS) {
    XELOGE("Failed to create a Vulkan device");
    return false;
  }

  // Get device functions.
  bool device_functions_loaded = true;
  device_functions_loaded &=
      (dfn_.getDeviceQueue = PFN_vkGetDeviceQueue(
           ifn_.getDeviceProcAddr(device_, "vkGetDeviceQueue"))) != nullptr;
  if (!device_functions_loaded) {
    XELOGE("Failed to get Vulkan device function pointers");
    return false;
  }

  // Get the queues.
  dfn_.getDeviceQueue(device_, queue_family_graphics_compute_, 0,
                      &queue_graphics_compute_);
  if (queue_family_sparse_binding != UINT32_MAX) {
    if (separate_sparse_binding_queue) {
      dfn_.getDeviceQueue(device_, queue_family_sparse_binding, 0,
                          &queue_sparse_binding_);
    } else {
      queue_sparse_binding_ = queue_graphics_compute_;
    }
  } else {
    queue_sparse_binding_ = VK_NULL_HANDLE;
  }

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

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
