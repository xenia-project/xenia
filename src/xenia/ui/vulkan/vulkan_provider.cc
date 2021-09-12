/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_provider.h"

#include <cfloat>
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

// TODO(Triang3l): Disable Vulkan validation before releasing a stable version.
DEFINE_bool(
    vulkan_validation, true,
    "Enable Vulkan validation (VK_LAYER_KHRONOS_validation). Messages will be "
    "written to the OS debug log.",
    "Vulkan");
DEFINE_int32(
    vulkan_device, -1,
    "Index of the physical device to use, or -1 for any compatible device.",
    "Vulkan");

namespace xe {
namespace ui {
namespace vulkan {

std::unique_ptr<VulkanProvider> VulkanProvider::Create() {
  std::unique_ptr<VulkanProvider> provider(new VulkanProvider);
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

VulkanProvider::~VulkanProvider() {
  for (size_t i = 0; i < size_t(HostSampler::kCount); ++i) {
    if (host_samplers_[i] != VK_NULL_HANDLE) {
      dfn_.vkDestroySampler(device_, host_samplers_[i], nullptr);
    }
  }

  if (device_ != VK_NULL_HANDLE) {
    ifn_.vkDestroyDevice(device_, nullptr);
  }
  if (instance_ != VK_NULL_HANDLE) {
    lfn_.vkDestroyInstance(instance_, nullptr);
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
  bool library_functions_loaded = true;
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
#define XE_VULKAN_LOAD_MODULE_LFN(name) \
  library_functions_loaded &=           \
      (lfn_.name = PFN_##name(dlsym(library_, #name))) != nullptr;
#elif XE_PLATFORM_WIN32
  library_ = LoadLibraryA("vulkan-1.dll");
  if (!library_) {
    XELOGE("Failed to load vulkan-1.dll");
    return false;
  }
#define XE_VULKAN_LOAD_MODULE_LFN(name) \
  library_functions_loaded &=           \
      (lfn_.name = PFN_##name(GetProcAddress(library_, #name))) != nullptr;
#else
#error No Vulkan library loading provided for the target platform.
#endif
  XE_VULKAN_LOAD_MODULE_LFN(vkGetInstanceProcAddr);
  XE_VULKAN_LOAD_MODULE_LFN(vkDestroyInstance);
#undef XE_VULKAN_LOAD_MODULE_LFN
  if (!library_functions_loaded) {
    XELOGE("Failed to get Vulkan library function pointers");
    return false;
  }
  library_functions_loaded &=
      (lfn_.vkCreateInstance = PFN_vkCreateInstance(lfn_.vkGetInstanceProcAddr(
           VK_NULL_HANDLE, "vkCreateInstance"))) != nullptr;
  library_functions_loaded &=
      (lfn_.vkEnumerateInstanceExtensionProperties =
           PFN_vkEnumerateInstanceExtensionProperties(
               lfn_.vkGetInstanceProcAddr(
                   VK_NULL_HANDLE,
                   "vkEnumerateInstanceExtensionProperties"))) != nullptr;
  if (!library_functions_loaded) {
    XELOGE(
        "Failed to get Vulkan library function pointers via "
        "vkGetInstanceProcAddr");
    return false;
  }
  lfn_.v_1_1.vkEnumerateInstanceVersion = PFN_vkEnumerateInstanceVersion(
      lfn_.vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));

  // Get the API version.
  const uint32_t api_version_target = VK_MAKE_VERSION(1, 2, 148);
  static_assert(VK_HEADER_VERSION_COMPLETE >= api_version_target,
                "Vulkan header files must be up to date");
  uint32_t instance_api_version;
  if (!lfn_.v_1_1.vkEnumerateInstanceVersion ||
      lfn_.v_1_1.vkEnumerateInstanceVersion(&instance_api_version) !=
          VK_SUCCESS) {
    instance_api_version = VK_API_VERSION_1_0;
  }
  XELOGVK("Vulkan instance version: {}.{}.{}",
          VK_VERSION_MAJOR(instance_api_version),
          VK_VERSION_MINOR(instance_api_version),
          VK_VERSION_PATCH(instance_api_version));

  // Get the instance extensions.
  std::vector<VkExtensionProperties> instance_extension_properties;
  VkResult instance_extensions_enumerate_result;
  for (;;) {
    uint32_t instance_extension_count =
        uint32_t(instance_extension_properties.size());
    bool instance_extensions_was_empty = !instance_extension_count;
    instance_extensions_enumerate_result =
        lfn_.vkEnumerateInstanceExtensionProperties(
            nullptr, &instance_extension_count,
            instance_extensions_was_empty
                ? nullptr
                : instance_extension_properties.data());
    // If the original extension count was 0 (first call), SUCCESS is
    // returned, not INCOMPLETE.
    if (instance_extensions_enumerate_result == VK_SUCCESS ||
        instance_extensions_enumerate_result == VK_INCOMPLETE) {
      instance_extension_properties.resize(instance_extension_count);
      if (instance_extensions_enumerate_result == VK_SUCCESS &&
          (!instance_extensions_was_empty || !instance_extension_count)) {
        break;
      }
    } else {
      break;
    }
  }
  if (instance_extensions_enumerate_result != VK_SUCCESS) {
    instance_extension_properties.clear();
  }
  std::memset(&instance_extensions_, 0, sizeof(instance_extensions_));
  if (instance_api_version >= VK_MAKE_VERSION(1, 1, 0)) {
    instance_extensions_.khr_get_physical_device_properties2 = true;
  }
  for (const VkExtensionProperties& instance_extension :
       instance_extension_properties) {
    const char* instance_extension_name = instance_extension.extensionName;
    if (!instance_extensions_.khr_get_physical_device_properties2 &&
        !std::strcmp(instance_extension_name,
                     "VK_KHR_get_physical_device_properties2")) {
      instance_extensions_.khr_get_physical_device_properties2 = true;
    }
  }
  XELOGVK("Vulkan instance extensions:");
  XELOGVK(
      "* VK_KHR_get_physical_device_properties2: {}",
      instance_extensions_.khr_get_physical_device_properties2 ? "yes" : "no");

  // Create the instance.
  std::vector<const char*> instance_extensions_enabled;
  instance_extensions_enabled.push_back("VK_KHR_surface");
#if XE_PLATFORM_ANDROID
  instance_extensions_enabled.push_back("VK_KHR_android_surface");
#elif XE_PLATFORM_WIN32
  instance_extensions_enabled.push_back("VK_KHR_win32_surface");
#endif
  if (instance_api_version < VK_MAKE_VERSION(1, 1, 0)) {
    if (instance_extensions_.khr_get_physical_device_properties2) {
      instance_extensions_enabled.push_back(
          "VK_KHR_get_physical_device_properties2");
    }
  }
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
  application_info.apiVersion = instance_api_version >= VK_MAKE_VERSION(1, 1, 0)
                                    ? api_version_target
                                    : instance_api_version;
  VkInstanceCreateInfo instance_create_info;
  instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_create_info.pNext = nullptr;
  instance_create_info.flags = 0;
  instance_create_info.pApplicationInfo = &application_info;
  static const char* validation_layer = "VK_LAYER_KHRONOS_validation";
  if (cvars::vulkan_validation) {
    instance_create_info.enabledLayerCount = 1;
    instance_create_info.ppEnabledLayerNames = &validation_layer;
  } else {
    instance_create_info.enabledLayerCount = 0;
    instance_create_info.ppEnabledLayerNames = nullptr;
  }
  instance_create_info.enabledExtensionCount =
      uint32_t(instance_extensions_enabled.size());
  instance_create_info.ppEnabledExtensionNames =
      instance_extensions_enabled.data();
  VkResult instance_create_result =
      lfn_.vkCreateInstance(&instance_create_info, nullptr, &instance_);
  if (instance_create_result != VK_SUCCESS) {
    if (instance_create_result == VK_ERROR_LAYER_NOT_PRESENT) {
      XELOGE("Failed to enable the Vulkan validation layer");
      instance_create_info.enabledLayerCount = 0;
      instance_create_info.ppEnabledLayerNames = nullptr;
      instance_create_result =
          lfn_.vkCreateInstance(&instance_create_info, nullptr, &instance_);
    }
    if (instance_create_result != VK_SUCCESS) {
      XELOGE("Failed to create a Vulkan instance with surface support");
      return false;
    }
  }

  // Get instance functions.
  std::memset(&ifn_, 0, sizeof(ifn_));
  bool instance_functions_loaded = true;
#define XE_VULKAN_LOAD_IFN(name) \
  instance_functions_loaded &=   \
      (ifn_.name = PFN_##name(   \
           lfn_.vkGetInstanceProcAddr(instance_, #name))) != nullptr;
#define XE_VULKAN_LOAD_IFN_SYMBOL(name, symbol) \
  instance_functions_loaded &=                  \
      (ifn_.name = PFN_##name(                  \
           lfn_.vkGetInstanceProcAddr(instance_, symbol))) != nullptr;
  XE_VULKAN_LOAD_IFN(vkCreateDevice);
  XE_VULKAN_LOAD_IFN(vkDestroyDevice);
  XE_VULKAN_LOAD_IFN(vkDestroySurfaceKHR);
  XE_VULKAN_LOAD_IFN(vkEnumerateDeviceExtensionProperties);
  XE_VULKAN_LOAD_IFN(vkEnumeratePhysicalDevices);
  XE_VULKAN_LOAD_IFN(vkGetDeviceProcAddr);
  XE_VULKAN_LOAD_IFN(vkGetPhysicalDeviceFeatures);
  XE_VULKAN_LOAD_IFN(vkGetPhysicalDeviceProperties);
  XE_VULKAN_LOAD_IFN(vkGetPhysicalDeviceMemoryProperties);
  XE_VULKAN_LOAD_IFN(vkGetPhysicalDeviceQueueFamilyProperties);
  XE_VULKAN_LOAD_IFN(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
  XE_VULKAN_LOAD_IFN(vkGetPhysicalDeviceSurfaceFormatsKHR);
  XE_VULKAN_LOAD_IFN(vkGetPhysicalDeviceSurfacePresentModesKHR);
  XE_VULKAN_LOAD_IFN(vkGetPhysicalDeviceSurfaceSupportKHR);
#if XE_PLATFORM_ANDROID
  XE_VULKAN_LOAD_IFN(vkCreateAndroidSurfaceKHR);
#elif XE_PLATFORM_WIN32
  XE_VULKAN_LOAD_IFN(vkCreateWin32SurfaceKHR);
#endif
  if (instance_extensions_.khr_get_physical_device_properties2) {
    XE_VULKAN_LOAD_IFN_SYMBOL(vkGetPhysicalDeviceProperties2KHR,
                              (instance_api_version >= VK_MAKE_VERSION(1, 1, 0))
                                  ? "vkGetPhysicalDeviceProperties2"
                                  : "vkGetPhysicalDeviceProperties2KHR");
  }
#undef XE_VULKAN_LOAD_IFN_SYMBOL
#undef XE_VULKAN_LOAD_IFN
  if (!instance_functions_loaded) {
    XELOGE("Failed to get Vulkan instance function pointers");
    return false;
  }

  // Get the compatible physical device.
  std::vector<VkPhysicalDevice> physical_devices;
  for (;;) {
    uint32_t physical_device_count = uint32_t(physical_devices.size());
    bool physical_devices_was_empty = !physical_device_count;
    VkResult physical_device_enumerate_result = ifn_.vkEnumeratePhysicalDevices(
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
    ifn_.vkGetPhysicalDeviceFeatures(physical_device_current,
                                     &device_features_);
    // Passing indices directly from guest memory, where they are big-endian; a
    // workaround using fetch from shared memory for 32-bit indices that need
    // swapping isn't implemented yet. Not supported only Qualcomm Adreno 4xx.
    if (!device_features_.fullDrawIndexUint32) {
      continue;
    }
    // TODO(Triang3l): Make geometry shaders optional by providing compute
    // shader fallback (though that would require vertex shader stores).
    if (!device_features_.geometryShader) {
      continue;
    }

    // Get the graphics and compute queue, and also a sparse binding queue
    // (preferably the same for the least latency between the two, as Xenia
    // submits sparse binding commands right before graphics commands anyway).
    uint32_t queue_family_count = 0;
    ifn_.vkGetPhysicalDeviceQueueFamilyProperties(physical_device_current,
                                                  &queue_family_count, nullptr);
    queue_families.resize(queue_family_count);
    ifn_.vkGetPhysicalDeviceQueueFamilyProperties(
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

    // Get device properties, will be needed to check if extensions have been
    // promoted to core.
    ifn_.vkGetPhysicalDeviceProperties(physical_device_current,
                                       &device_properties_);

    // Get the extensions, check if swapchain is supported.
    device_extension_properties.clear();
    VkResult device_extensions_enumerate_result;
    for (;;) {
      uint32_t device_extension_count =
          uint32_t(device_extension_properties.size());
      bool device_extensions_was_empty = !device_extension_count;
      device_extensions_enumerate_result =
          ifn_.vkEnumerateDeviceExtensionProperties(
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
    if (device_properties_.apiVersion >= VK_MAKE_VERSION(1, 1, 0)) {
      device_extensions_.khr_dedicated_allocation = true;
      if (device_properties_.apiVersion >= VK_MAKE_VERSION(1, 2, 0)) {
        device_extensions_.khr_shader_float_controls = true;
        device_extensions_.khr_spirv_1_4 = true;
      }
    }
    bool device_supports_swapchain = false;
    for (const VkExtensionProperties& device_extension :
         device_extension_properties) {
      const char* device_extension_name = device_extension.extensionName;
      if (!device_extensions_.ext_fragment_shader_interlock &&
          !std::strcmp(device_extension_name,
                       "VK_EXT_fragment_shader_interlock")) {
        device_extensions_.ext_fragment_shader_interlock = true;
      } else if (!device_extensions_.khr_dedicated_allocation &&
                 !std::strcmp(device_extension_name,
                              "VK_KHR_dedicated_allocation")) {
        device_extensions_.khr_dedicated_allocation = true;
      } else if (!device_extensions_.khr_shader_float_controls &&
                 !std::strcmp(device_extension_name,
                              "VK_KHR_shader_float_controls")) {
        device_extensions_.khr_shader_float_controls = true;
      } else if (!device_extensions_.khr_spirv_1_4 &&
                 !std::strcmp(device_extension_name, "VK_KHR_spirv_1_4")) {
        device_extensions_.khr_spirv_1_4 = true;
      } else if (!device_supports_swapchain &&
                 !std::strcmp(device_extension_name, "VK_KHR_swapchain")) {
        device_supports_swapchain = true;
      }
    }
    if (!device_supports_swapchain) {
      continue;
    }

    // Get the memory types.
    VkPhysicalDeviceMemoryProperties memory_properties;
    ifn_.vkGetPhysicalDeviceMemoryProperties(physical_device_current,
                                             &memory_properties);
    memory_types_device_local_ = 0;
    memory_types_host_visible_ = 0;
    memory_types_host_coherent_ = 0;
    memory_types_host_cached_ = 0;
    for (uint32_t j = 0; j < memory_properties.memoryTypeCount; ++j) {
      VkMemoryPropertyFlags memory_property_flags =
          memory_properties.memoryTypes[j].propertyFlags;
      uint32_t memory_type_bit = uint32_t(1) << j;
      if (memory_property_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
        memory_types_device_local_ |= memory_type_bit;
      }
      if (memory_property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        memory_types_host_visible_ |= memory_type_bit;
      }
      if (memory_property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
        memory_types_host_coherent_ |= memory_type_bit;
      }
      if (memory_property_flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
        memory_types_host_cached_ |= memory_type_bit;
      }
    }
    if (!memory_types_device_local_ && !memory_types_host_visible_) {
      // Shouldn't happen according to the specification.
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

  // Get additional device properties.
  std::memset(&device_float_controls_properties_, 0,
              sizeof(device_float_controls_properties_));
  if (instance_extensions_.khr_get_physical_device_properties2) {
    VkPhysicalDeviceProperties2KHR device_properties_2;
    device_properties_2.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR;
    device_properties_2.pNext = nullptr;
    VkPhysicalDeviceProperties2KHR* device_properties_2_last =
        &device_properties_2;
    if (device_extensions_.khr_shader_float_controls) {
      device_float_controls_properties_.sType =
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES_KHR;
      device_float_controls_properties_.pNext = nullptr;
      device_properties_2_last->pNext = &device_float_controls_properties_;
      device_properties_2_last =
          reinterpret_cast<VkPhysicalDeviceProperties2KHR*>(
              &device_float_controls_properties_);
    }
    if (device_properties_2_last != &device_properties_2) {
      ifn_.vkGetPhysicalDeviceProperties2KHR(physical_device_,
                                             &device_properties_2);
    }
  }

  XELOGVK(
      "Vulkan device: {} (vendor {:04X}, device {:04X}, driver {:08X}, API "
      "{}.{}.{})",
      device_properties_.deviceName, device_properties_.vendorID,
      device_properties_.deviceID, device_properties_.driverVersion,
      VK_VERSION_MAJOR(device_properties_.apiVersion),
      VK_VERSION_MINOR(device_properties_.apiVersion),
      VK_VERSION_PATCH(device_properties_.apiVersion));
  XELOGVK("Vulkan device extensions:");
  XELOGVK("* VK_EXT_fragment_shader_interlock: {}",
          device_extensions_.ext_fragment_shader_interlock ? "yes" : "no");
  XELOGVK("* VK_KHR_dedicated_allocation: {}",
          device_extensions_.khr_dedicated_allocation ? "yes" : "no");
  XELOGVK("* VK_KHR_shader_float_controls: {}",
          device_extensions_.khr_shader_float_controls ? "yes" : "no");
  XELOGVK("* VK_KHR_spirv_1_4: {}",
          device_extensions_.khr_spirv_1_4 ? "yes" : "no");
  if (device_extensions_.khr_shader_float_controls) {
    XELOGVK(
        "* Signed zero, inf, nan preserve for float32: {}",
        device_float_controls_properties_.shaderSignedZeroInfNanPreserveFloat32
            ? "yes"
            : "no");
    XELOGVK("* Denorm flush to zero for float32: {}",
            device_float_controls_properties_.shaderDenormFlushToZeroFloat32
                ? "yes"
                : "no");
  }
  // TODO(Triang3l): Report properties, features.

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
  if (device_properties_.apiVersion < VK_MAKE_VERSION(1, 2, 0)) {
    if (device_properties_.apiVersion < VK_MAKE_VERSION(1, 1, 0)) {
      if (device_extensions_.khr_dedicated_allocation) {
        device_extensions_enabled.push_back("VK_KHR_dedicated_allocation");
      }
    }
    if (device_extensions_.khr_shader_float_controls) {
      device_extensions_enabled.push_back("VK_KHR_shader_float_controls");
    }
    if (device_extensions_.khr_spirv_1_4) {
      device_extensions_enabled.push_back("VK_KHR_spirv_1_4");
    }
  }
  VkDeviceCreateInfo device_create_info;
  device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_create_info.pNext = nullptr;
  device_create_info.flags = 0;
  device_create_info.queueCreateInfoCount =
      separate_sparse_binding_queue ? 2 : 1;
  device_create_info.pQueueCreateInfos = queue_create_infos;
  // Device layers are deprecated - using validation layer on the instance.
  device_create_info.enabledLayerCount = 0;
  device_create_info.ppEnabledLayerNames = nullptr;
  device_create_info.enabledExtensionCount =
      uint32_t(device_extensions_enabled.size());
  device_create_info.ppEnabledExtensionNames = device_extensions_enabled.data();
  // TODO(Triang3l): Enable only needed features.
  device_create_info.pEnabledFeatures = &device_features_;
  if (ifn_.vkCreateDevice(physical_device_, &device_create_info, nullptr,
                          &device_) != VK_SUCCESS) {
    XELOGE("Failed to create a Vulkan device");
    return false;
  }

  // Get device functions.
  bool device_functions_loaded = true;
#define XE_VULKAN_LOAD_DFN(name)                                            \
  device_functions_loaded &=                                                \
      (dfn_.name = PFN_##name(ifn_.vkGetDeviceProcAddr(device_, #name))) != \
      nullptr;
  XE_VULKAN_LOAD_DFN(vkAcquireNextImageKHR);
  XE_VULKAN_LOAD_DFN(vkAllocateCommandBuffers);
  XE_VULKAN_LOAD_DFN(vkAllocateDescriptorSets);
  XE_VULKAN_LOAD_DFN(vkAllocateMemory);
  XE_VULKAN_LOAD_DFN(vkBeginCommandBuffer);
  XE_VULKAN_LOAD_DFN(vkBindBufferMemory);
  XE_VULKAN_LOAD_DFN(vkBindImageMemory);
  XE_VULKAN_LOAD_DFN(vkCmdBeginRenderPass);
  XE_VULKAN_LOAD_DFN(vkCmdBindDescriptorSets);
  XE_VULKAN_LOAD_DFN(vkCmdBindIndexBuffer);
  XE_VULKAN_LOAD_DFN(vkCmdBindPipeline);
  XE_VULKAN_LOAD_DFN(vkCmdBindVertexBuffers);
  XE_VULKAN_LOAD_DFN(vkCmdClearColorImage);
  XE_VULKAN_LOAD_DFN(vkCmdCopyBuffer);
  XE_VULKAN_LOAD_DFN(vkCmdCopyBufferToImage);
  XE_VULKAN_LOAD_DFN(vkCmdDraw);
  XE_VULKAN_LOAD_DFN(vkCmdDrawIndexed);
  XE_VULKAN_LOAD_DFN(vkCmdEndRenderPass);
  XE_VULKAN_LOAD_DFN(vkCmdPipelineBarrier);
  XE_VULKAN_LOAD_DFN(vkCmdPushConstants);
  XE_VULKAN_LOAD_DFN(vkCmdSetScissor);
  XE_VULKAN_LOAD_DFN(vkCmdSetViewport);
  XE_VULKAN_LOAD_DFN(vkCreateBuffer);
  XE_VULKAN_LOAD_DFN(vkCreateCommandPool);
  XE_VULKAN_LOAD_DFN(vkCreateDescriptorPool);
  XE_VULKAN_LOAD_DFN(vkCreateDescriptorSetLayout);
  XE_VULKAN_LOAD_DFN(vkCreateFence);
  XE_VULKAN_LOAD_DFN(vkCreateFramebuffer);
  XE_VULKAN_LOAD_DFN(vkCreateGraphicsPipelines);
  XE_VULKAN_LOAD_DFN(vkCreateImage);
  XE_VULKAN_LOAD_DFN(vkCreateImageView);
  XE_VULKAN_LOAD_DFN(vkCreatePipelineLayout);
  XE_VULKAN_LOAD_DFN(vkCreateRenderPass);
  XE_VULKAN_LOAD_DFN(vkCreateSampler);
  XE_VULKAN_LOAD_DFN(vkCreateSemaphore);
  XE_VULKAN_LOAD_DFN(vkCreateShaderModule);
  XE_VULKAN_LOAD_DFN(vkCreateSwapchainKHR);
  XE_VULKAN_LOAD_DFN(vkDestroyBuffer);
  XE_VULKAN_LOAD_DFN(vkDestroyCommandPool);
  XE_VULKAN_LOAD_DFN(vkDestroyDescriptorPool);
  XE_VULKAN_LOAD_DFN(vkDestroyDescriptorSetLayout);
  XE_VULKAN_LOAD_DFN(vkDestroyFence);
  XE_VULKAN_LOAD_DFN(vkDestroyFramebuffer);
  XE_VULKAN_LOAD_DFN(vkDestroyImage);
  XE_VULKAN_LOAD_DFN(vkDestroyImageView);
  XE_VULKAN_LOAD_DFN(vkDestroyPipeline);
  XE_VULKAN_LOAD_DFN(vkDestroyPipelineLayout);
  XE_VULKAN_LOAD_DFN(vkDestroyRenderPass);
  XE_VULKAN_LOAD_DFN(vkDestroySampler);
  XE_VULKAN_LOAD_DFN(vkDestroySemaphore);
  XE_VULKAN_LOAD_DFN(vkDestroyShaderModule);
  XE_VULKAN_LOAD_DFN(vkDestroySwapchainKHR);
  XE_VULKAN_LOAD_DFN(vkEndCommandBuffer);
  XE_VULKAN_LOAD_DFN(vkFlushMappedMemoryRanges);
  XE_VULKAN_LOAD_DFN(vkFreeMemory);
  XE_VULKAN_LOAD_DFN(vkGetBufferMemoryRequirements);
  XE_VULKAN_LOAD_DFN(vkGetDeviceQueue);
  XE_VULKAN_LOAD_DFN(vkGetImageMemoryRequirements);
  XE_VULKAN_LOAD_DFN(vkGetSwapchainImagesKHR);
  XE_VULKAN_LOAD_DFN(vkMapMemory);
  XE_VULKAN_LOAD_DFN(vkResetCommandPool);
  XE_VULKAN_LOAD_DFN(vkResetDescriptorPool);
  XE_VULKAN_LOAD_DFN(vkResetFences);
  XE_VULKAN_LOAD_DFN(vkQueueBindSparse);
  XE_VULKAN_LOAD_DFN(vkQueuePresentKHR);
  XE_VULKAN_LOAD_DFN(vkQueueSubmit);
  XE_VULKAN_LOAD_DFN(vkUnmapMemory);
  XE_VULKAN_LOAD_DFN(vkUpdateDescriptorSets);
  XE_VULKAN_LOAD_DFN(vkWaitForFences);
#undef XE_VULKAN_LOAD_DFN
  if (!device_functions_loaded) {
    XELOGE("Failed to get Vulkan device function pointers");
    return false;
  }

  // Get the queues.
  dfn_.vkGetDeviceQueue(device_, queue_family_graphics_compute_, 0,
                        &queue_graphics_compute_);
  if (queue_family_sparse_binding != UINT32_MAX) {
    if (separate_sparse_binding_queue) {
      dfn_.vkGetDeviceQueue(device_, queue_family_sparse_binding, 0,
                            &queue_sparse_binding_);
    } else {
      queue_sparse_binding_ = queue_graphics_compute_;
    }
  } else {
    queue_sparse_binding_ = VK_NULL_HANDLE;
  }

  // Create host-side samplers.
  VkSamplerCreateInfo sampler_create_info = {};
  sampler_create_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_create_info.magFilter = VK_FILTER_NEAREST;
  sampler_create_info.minFilter = VK_FILTER_NEAREST;
  sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_create_info.maxLod = FLT_MAX;
  if (dfn_.vkCreateSampler(
          device_, &sampler_create_info, nullptr,
          &host_samplers_[size_t(HostSampler::kNearestClamp)]) != VK_SUCCESS) {
    XELOGE("Failed to create the nearest-neighbor clamping Vulkan sampler");
    return false;
  }
  sampler_create_info.magFilter = VK_FILTER_LINEAR;
  sampler_create_info.minFilter = VK_FILTER_LINEAR;
  sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  if (dfn_.vkCreateSampler(
          device_, &sampler_create_info, nullptr,
          &host_samplers_[size_t(HostSampler::kLinearClamp)]) != VK_SUCCESS) {
    XELOGE("Failed to create the bilinear-filtering clamping Vulkan sampler");
    return false;
  }
  sampler_create_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_create_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  sampler_create_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  if (dfn_.vkCreateSampler(
          device_, &sampler_create_info, nullptr,
          &host_samplers_[size_t(HostSampler::kLinearRepeat)]) != VK_SUCCESS) {
    XELOGE("Failed to create the bilinear-filtering repeating Vulkan sampler");
    return false;
  }
  sampler_create_info.magFilter = VK_FILTER_NEAREST;
  sampler_create_info.minFilter = VK_FILTER_NEAREST;
  sampler_create_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  if (dfn_.vkCreateSampler(
          device_, &sampler_create_info, nullptr,
          &host_samplers_[size_t(HostSampler::kNearestRepeat)]) != VK_SUCCESS) {
    XELOGE("Failed to create the nearest-neighbor repeating Vulkan sampler");
    return false;
  }

  return true;
}

std::unique_ptr<GraphicsContext> VulkanProvider::CreateHostContext(
    Window* target_window) {
  auto new_context =
      std::unique_ptr<VulkanContext>(new VulkanContext(this, target_window));
  if (!new_context->Initialize()) {
    return nullptr;
  }
  return std::unique_ptr<GraphicsContext>(new_context.release());
}

std::unique_ptr<GraphicsContext> VulkanProvider::CreateEmulationContext() {
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
