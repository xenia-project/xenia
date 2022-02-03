/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
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
#include "xenia/base/math.h"
#include "xenia/base/platform.h"
#include "xenia/ui/vulkan/vulkan_immediate_drawer.h"
#include "xenia/ui/vulkan/vulkan_presenter.h"

#if XE_PLATFORM_LINUX
#include <dlfcn.h>
#elif XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

// TODO(Triang3l): Disable Vulkan validation before releasing a stable version.
DEFINE_bool(
    vulkan_validation, true,
    "Enable Vulkan validation (VK_LAYER_KHRONOS_validation). Messages will be "
    "written to the OS debug log without vulkan_debug_messenger or to the "
    "Xenia log with it.",
    "Vulkan");
DEFINE_bool(
    vulkan_debug_utils_messenger, false,
    "Enable writing Vulkan debug messages via VK_EXT_debug_utils to the Xenia "
    "log.",
    "Vulkan");
DEFINE_uint32(
    vulkan_debug_utils_messenger_severity, 2,
    "Maximum severity of messages to log via the Vulkan debug messenger: 0 - "
    "error, 1 - warning, 2 - info, 3 - verbose.",
    "Vulkan");
DEFINE_bool(vulkan_debug_utils_names, false,
            "Enable naming Vulkan objects via VK_EXT_debug_utils.", "Vulkan");
DEFINE_int32(
    vulkan_device, -1,
    "Index of the physical device to use, or -1 for any compatible device.",
    "Vulkan");

namespace xe {
namespace ui {
namespace vulkan {

std::unique_ptr<VulkanProvider> VulkanProvider::Create(
    bool is_surface_required) {
  std::unique_ptr<VulkanProvider> provider(
      new VulkanProvider(is_surface_required));
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
    if (debug_messenger_ != VK_NULL_HANDLE) {
      ifn_.vkDestroyDebugUtilsMessengerEXT(instance_, debug_messenger_,
                                           nullptr);
    }
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
  renderdoc_api_.Initialize();

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
  library_functions_loaded &=
      (lfn_.vkEnumerateInstanceLayerProperties =
           PFN_vkEnumerateInstanceLayerProperties(lfn_.vkGetInstanceProcAddr(
               VK_NULL_HANDLE, "vkEnumerateInstanceLayerProperties"))) !=
      nullptr;
  if (!library_functions_loaded) {
    XELOGE(
        "Failed to get Vulkan library function pointers via "
        "vkGetInstanceProcAddr");
    return false;
  }
  lfn_.v_1_1.vkEnumerateInstanceVersion = PFN_vkEnumerateInstanceVersion(
      lfn_.vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));

  // Get the API version.
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

  // Get the instance extensions without layers, as well as extensions promoted
  // to the core.
  bool debug_utils_messenger_requested = cvars::vulkan_debug_utils_messenger;
  bool debug_utils_names_requested = cvars::vulkan_debug_utils_names;
  bool debug_utils_requested =
      debug_utils_messenger_requested || debug_utils_names_requested;
  std::memset(&instance_extensions_, 0, sizeof(instance_extensions_));
  if (instance_api_version >= VK_MAKE_API_VERSION(0, 1, 1, 0)) {
    instance_extensions_.khr_get_physical_device_properties2 = true;
  }
  std::vector<const char*> instance_extensions_enabled;
  std::vector<VkExtensionProperties> instance_or_layer_extension_properties;
  VkResult instance_extensions_enumerate_result;
  for (;;) {
    uint32_t instance_extension_count =
        uint32_t(instance_or_layer_extension_properties.size());
    bool instance_extensions_were_empty = !instance_extension_count;
    instance_extensions_enumerate_result =
        lfn_.vkEnumerateInstanceExtensionProperties(
            nullptr, &instance_extension_count,
            instance_extensions_were_empty
                ? nullptr
                : instance_or_layer_extension_properties.data());
    // If the original extension count was 0 (first call), SUCCESS is returned,
    // not INCOMPLETE.
    if (instance_extensions_enumerate_result == VK_SUCCESS ||
        instance_extensions_enumerate_result == VK_INCOMPLETE) {
      instance_or_layer_extension_properties.resize(instance_extension_count);
      if (instance_extensions_enumerate_result == VK_SUCCESS &&
          (!instance_extensions_were_empty || !instance_extension_count)) {
        break;
      }
    } else {
      break;
    }
  }
  if (instance_extensions_enumerate_result == VK_SUCCESS) {
    AccumulateInstanceExtensions(instance_or_layer_extension_properties.size(),
                                 instance_or_layer_extension_properties.data(),
                                 debug_utils_requested, instance_extensions_,
                                 instance_extensions_enabled);
  }
  size_t instance_extensions_enabled_count_without_layers =
      instance_extensions_enabled.size();
  InstanceExtensions instance_extensions_without_layers = instance_extensions_;

  // Get the instance layers and their extensions.
  std::vector<VkLayerProperties> layer_properties;
  VkResult layers_enumerate_result;
  for (;;) {
    uint32_t layer_count = uint32_t(layer_properties.size());
    bool layers_were_empty = !layer_count;
    layers_enumerate_result = lfn_.vkEnumerateInstanceLayerProperties(
        &layer_count, layers_were_empty ? nullptr : layer_properties.data());
    // If the original layer count was 0 (first call), SUCCESS is returned, not
    // INCOMPLETE.
    if (layers_enumerate_result == VK_SUCCESS ||
        layers_enumerate_result == VK_INCOMPLETE) {
      layer_properties.resize(layer_count);
      if (layers_enumerate_result == VK_SUCCESS &&
          (!layers_were_empty || !layer_count)) {
        break;
      }
    } else {
      break;
    }
  }
  if (layers_enumerate_result != VK_SUCCESS) {
    layer_properties.clear();
  }
  struct {
    bool khronos_validation;
  } layer_enabled_flags = {};
  std::vector<const char*> layers_enabled;
  for (const VkLayerProperties& layer : layer_properties) {
    // Check if the layer is needed.
    // Checking if already enabled as an optimization to do fewer and fewer
    // string comparisons. Adding literals to layers_enabled for the most C
    // string lifetime safety.
    if (!layer_enabled_flags.khronos_validation && cvars::vulkan_validation &&
        !std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation")) {
      layers_enabled.push_back("VK_LAYER_KHRONOS_validation");
      layer_enabled_flags.khronos_validation = true;
    } else {
      // Not enabling this layer, so don't need the extensions from it as well.
      continue;
    }
    // Load extensions from the layer.
    instance_or_layer_extension_properties.clear();
    for (;;) {
      uint32_t instance_extension_count =
          uint32_t(instance_or_layer_extension_properties.size());
      bool instance_extensions_were_empty = !instance_extension_count;
      instance_extensions_enumerate_result =
          lfn_.vkEnumerateInstanceExtensionProperties(
              layer.layerName, &instance_extension_count,
              instance_extensions_were_empty
                  ? nullptr
                  : instance_or_layer_extension_properties.data());
      // If the original extension count was 0 (first call), SUCCESS is
      // returned, not INCOMPLETE.
      if (instance_extensions_enumerate_result == VK_SUCCESS ||
          instance_extensions_enumerate_result == VK_INCOMPLETE) {
        instance_or_layer_extension_properties.resize(instance_extension_count);
        if (instance_extensions_enumerate_result == VK_SUCCESS &&
            (!instance_extensions_were_empty || !instance_extension_count)) {
          break;
        }
      } else {
        break;
      }
    }
    if (instance_extensions_enumerate_result == VK_SUCCESS) {
      AccumulateInstanceExtensions(
          instance_or_layer_extension_properties.size(),
          instance_or_layer_extension_properties.data(), debug_utils_requested,
          instance_extensions_, instance_extensions_enabled);
    }
  }

  // Create the instance.
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
  application_info.apiVersion =
      instance_api_version >= VK_MAKE_API_VERSION(0, 1, 1, 0)
          ? VK_HEADER_VERSION_COMPLETE
          : instance_api_version;
  VkInstanceCreateInfo instance_create_info;
  instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_create_info.pNext = nullptr;
  instance_create_info.flags = 0;
  instance_create_info.pApplicationInfo = &application_info;
  instance_create_info.enabledLayerCount = uint32_t(layers_enabled.size());
  instance_create_info.ppEnabledLayerNames = layers_enabled.data();
  instance_create_info.enabledExtensionCount =
      uint32_t(instance_extensions_enabled.size());
  instance_create_info.ppEnabledExtensionNames =
      instance_extensions_enabled.data();
  VkResult instance_create_result =
      lfn_.vkCreateInstance(&instance_create_info, nullptr, &instance_);
  if (instance_create_result != VK_SUCCESS) {
    if ((instance_create_result == VK_ERROR_LAYER_NOT_PRESENT ||
         instance_create_result == VK_ERROR_EXTENSION_NOT_PRESENT) &&
        !layers_enabled.empty()) {
      XELOGE("Failed to enable Vulkan layers");
      // Try to create without layers and their extensions.
      std::memset(&layer_enabled_flags, 0, sizeof(layer_enabled_flags));
      instance_create_info.enabledLayerCount = 0;
      instance_create_info.ppEnabledLayerNames = nullptr;
      instance_create_info.enabledExtensionCount =
          uint32_t(instance_extensions_enabled_count_without_layers);
      instance_extensions_ = instance_extensions_without_layers;
      instance_create_result =
          lfn_.vkCreateInstance(&instance_create_info, nullptr, &instance_);
    }
    if (instance_create_result != VK_SUCCESS) {
      XELOGE("Failed to create a Vulkan instance");
      return false;
    }
  }

  // Get instance functions.
  std::memset(&ifn_, 0, sizeof(ifn_));
#define XE_UI_VULKAN_FUNCTION(name)                                       \
  functions_loaded &= (ifn_.name = PFN_##name(lfn_.vkGetInstanceProcAddr( \
                           instance_, #name))) != nullptr;
#define XE_UI_VULKAN_FUNCTION_DONT_PROMOTE(extension_name, core_name)         \
  functions_loaded &=                                                         \
      (ifn_.extension_name = PFN_##extension_name(lfn_.vkGetInstanceProcAddr( \
           instance_, #extension_name))) != nullptr;
#define XE_UI_VULKAN_FUNCTION_PROMOTE(extension_name, core_name) \
  functions_loaded &=                                            \
      (ifn_.extension_name = PFN_##extension_name(               \
           lfn_.vkGetInstanceProcAddr(instance_, #core_name))) != nullptr;
  // Core - require unconditionally.
  {
    bool functions_loaded = true;
#include "xenia/ui/vulkan/functions/instance_1_0.inc"
    if (!functions_loaded) {
      XELOGE("Failed to get Vulkan instance function pointers");
      return false;
    }
  }
  // Extensions - disable the specific extension if failed to get its functions.
  if (instance_extensions_.ext_debug_utils) {
    bool functions_loaded = true;
#include "xenia/ui/vulkan/functions/instance_ext_debug_utils.inc"
    instance_extensions_.ext_debug_utils = functions_loaded;
  }
  if (instance_extensions_.khr_get_physical_device_properties2) {
    bool functions_loaded = true;
    if (instance_api_version >= VK_MAKE_API_VERSION(0, 1, 1, 0)) {
#define XE_UI_VULKAN_FUNCTION_PROMOTED XE_UI_VULKAN_FUNCTION_PROMOTE
#include "xenia/ui/vulkan/functions/instance_khr_get_physical_device_properties2.inc"
#undef XE_UI_VULKAN_FUNCTION_PROMOTED
    } else {
#define XE_UI_VULKAN_FUNCTION_PROMOTED XE_UI_VULKAN_FUNCTION_DONT_PROMOTE
#include "xenia/ui/vulkan/functions/instance_khr_get_physical_device_properties2.inc"
#undef XE_UI_VULKAN_FUNCTION_PROMOTED
    }
    instance_extensions_.khr_get_physical_device_properties2 = functions_loaded;
  }
  if (instance_extensions_.khr_surface) {
    bool functions_loaded = true;
#include "xenia/ui/vulkan/functions/instance_khr_surface.inc"
    instance_extensions_.khr_surface = functions_loaded;
  }
#if XE_PLATFORM_ANDROID
  if (instance_extensions_.khr_android_surface) {
    bool functions_loaded = true;
#include "xenia/ui/vulkan/functions/instance_khr_android_surface.inc"
    instance_extensions_.khr_android_surface = functions_loaded;
  }
#elif XE_PLATFORM_GNU_LINUX
  if (instance_extensions_.khr_xcb_surface) {
    bool functions_loaded = true;
#include "xenia/ui/vulkan/functions/instance_khr_xcb_surface.inc"
    instance_extensions_.khr_xcb_surface = functions_loaded;
  }
#elif XE_PLATFORM_WIN32
  if (instance_extensions_.khr_win32_surface) {
    bool functions_loaded = true;
#include "xenia/ui/vulkan/functions/instance_khr_win32_surface.inc"
    instance_extensions_.khr_win32_surface = functions_loaded;
  }
#endif  // XE_PLATFORM
#undef XE_UI_VULKAN_FUNCTION_PROMOTE
#undef XE_UI_VULKAN_FUNCTION_DONT_PROMOTE
#undef XE_UI_VULKAN_FUNCTION

  // Check if surface is supported after verifying that surface extension
  // function pointers could be obtained.
  if (is_surface_required_ &&
      !VulkanPresenter::GetSurfaceTypesSupportedByInstance(
          instance_extensions_)) {
    XELOGE(
        "The Vulkan instance doesn't support the required surface extension "
        "for the platform");
    return false;
  }

  // Report instance information after verifying that extension function
  // pointers could be obtained.
  XELOGVK("Vulkan layers enabled by Xenia:");
  XELOGVK("* VK_LAYER_KHRONOS_validation: {}",
          layer_enabled_flags.khronos_validation ? "yes" : "no");
  XELOGVK("Vulkan instance extensions:");
  XELOGVK("* VK_EXT_debug_utils: {}",
          instance_extensions_.ext_debug_utils
              ? "yes"
              : (debug_utils_requested ? "no" : "not requested"));
  XELOGVK(
      "* VK_KHR_get_physical_device_properties2: {}",
      instance_extensions_.khr_get_physical_device_properties2 ? "yes" : "no");
  XELOGVK("* VK_KHR_surface: {}",
          instance_extensions_.khr_surface ? "yes" : "no");
#if XE_PLATFORM_ANDROID
  XELOGVK("  * VK_KHR_android_surface: {}",
          instance_extensions_.khr_android_surface ? "yes" : "no");
#elif XE_PLATFORM_GNU_LINUX
  XELOGVK("  * VK_KHR_xcb_surface: {}",
          instance_extensions_.khr_xcb_surface ? "yes" : "no");
#elif XE_PLATFORM_WIN32
  XELOGVK("  * VK_KHR_win32_surface: {}",
          instance_extensions_.khr_win32_surface ? "yes" : "no");
#endif

  // Enable the debug messenger.
  if (debug_utils_messenger_requested) {
    if (instance_extensions_.ext_debug_utils) {
      VkDebugUtilsMessengerCreateInfoEXT debug_messenger_create_info;
      debug_messenger_create_info.sType =
          VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
      debug_messenger_create_info.pNext = nullptr;
      debug_messenger_create_info.flags = 0;
      debug_messenger_create_info.messageSeverity =
          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
      if (cvars::vulkan_debug_utils_messenger_severity >= 1) {
        debug_messenger_create_info.messageSeverity |=
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        if (cvars::vulkan_debug_utils_messenger_severity >= 2) {
          debug_messenger_create_info.messageSeverity |=
              VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
          if (cvars::vulkan_debug_utils_messenger_severity >= 3) {
            debug_messenger_create_info.messageSeverity |=
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
          }
        }
      }
      debug_messenger_create_info.messageType =
          VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
          VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      debug_messenger_create_info.pfnUserCallback = DebugMessengerCallback;
      debug_messenger_create_info.pUserData = this;
      ifn_.vkCreateDebugUtilsMessengerEXT(
          instance_, &debug_messenger_create_info, nullptr, &debug_messenger_);
    }
    if (debug_messenger_ != VK_NULL_HANDLE) {
      XELOGVK("Vulkan debug messenger enabled");
    } else {
      XELOGE("Failed to enable the Vulkan debug messenger");
    }
  }
  debug_names_used_ =
      debug_utils_names_requested && instance_extensions_.ext_debug_utils;

  // Get the compatible physical device.
  std::vector<VkPhysicalDevice> physical_devices;
  for (;;) {
    uint32_t physical_device_count = uint32_t(physical_devices.size());
    bool physical_devices_were_empty = !physical_device_count;
    VkResult physical_device_enumerate_result = ifn_.vkEnumeratePhysicalDevices(
        instance_, &physical_device_count,
        physical_devices_were_empty ? nullptr : physical_devices.data());
    // If the original device count was 0 (first call), SUCCESS is returned, not
    // INCOMPLETE.
    if (physical_device_enumerate_result == VK_SUCCESS ||
        physical_device_enumerate_result == VK_INCOMPLETE) {
      physical_devices.resize(physical_device_count);
      if (physical_device_enumerate_result == VK_SUCCESS &&
          (!physical_devices_were_empty || !physical_device_count)) {
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
  std::vector<VkQueueFamilyProperties> queue_families_properties;
  std::vector<VkExtensionProperties> device_extension_properties;
  std::vector<const char*> device_extensions_enabled;
  for (size_t i = physical_device_index_first; i <= physical_device_index_last;
       ++i) {
    VkPhysicalDevice physical_device_current = physical_devices[i];

    // Get physical device features and check if the needed ones are supported.
    // Need this before obtaining the queues as sparse binding is an optional
    // feature.
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

    // Get the needed queues:
    // - Graphics and compute.
    // - Sparse binding if used (preferably the same as the graphics and compute
    //   one for the lowest latency as Xenia submits sparse binding commands
    //   right before graphics commands anyway).
    // - Additional queues for presentation as VulkanProvider may be used with
    //   different surfaces, and they may have varying support of presentation
    //   from different queue families.
    uint32_t queue_family_count = 0;
    ifn_.vkGetPhysicalDeviceQueueFamilyProperties(physical_device_current,
                                                  &queue_family_count, nullptr);
    queue_families_properties.resize(queue_family_count);
    ifn_.vkGetPhysicalDeviceQueueFamilyProperties(
        physical_device_current, &queue_family_count,
        queue_families_properties.data());
    assert_true(queue_family_count == queue_families_properties.size());
    // Initialize all queue families to unused.
    queue_families_.clear();
    queue_families_.resize(queue_family_count);
    // First, try to obtain a graphics and compute queue. Preferably find a
    // queue with sparse binding support as well.
    // The family indices here are listed from the best to the worst.
    uint32_t queue_family_graphics_compute_sparse_binding = UINT32_MAX;
    uint32_t queue_family_graphics_compute_only = UINT32_MAX;
    for (uint32_t j = 0; j < queue_family_count; ++j) {
      const VkQueueFamilyProperties& queue_family_properties =
          queue_families_properties[j];
      if ((queue_family_properties.queueFlags &
           (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) !=
          (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
        continue;
      }
      uint32_t* queue_family_ptr;
      if (device_features_.sparseBinding &&
          (queue_family_properties.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT)) {
        queue_family_ptr = &queue_family_graphics_compute_sparse_binding;
      } else {
        queue_family_ptr = &queue_family_graphics_compute_only;
      }
      if (*queue_family_ptr == UINT32_MAX) {
        *queue_family_ptr = j;
      }
    }
    if (queue_family_graphics_compute_sparse_binding != UINT32_MAX) {
      assert_true(device_features_.sparseBinding);
      queue_family_graphics_compute_ =
          queue_family_graphics_compute_sparse_binding;
    } else if (queue_family_graphics_compute_only != UINT32_MAX) {
      queue_family_graphics_compute_ = queue_family_graphics_compute_only;
    } else {
      // No graphics and compute queue family.
      continue;
    }
    // Mark the graphics and compute queue as requested.
    queue_families_[queue_family_graphics_compute_].queue_count =
        std::max(queue_families_[queue_family_graphics_compute_].queue_count,
                 uint32_t(1));
    // Request a separate sparse binding queue if needed.
    queue_family_sparse_binding_ = UINT32_MAX;
    if (device_features_.sparseBinding) {
      if (queue_families_properties[queue_family_graphics_compute_].queueFlags &
          VK_QUEUE_SPARSE_BINDING_BIT) {
        queue_family_sparse_binding_ = queue_family_graphics_compute_;
      } else {
        for (uint32_t j = 0; j < queue_family_count; ++j) {
          if (!(queue_families_properties[j].queueFlags &
                VK_QUEUE_SPARSE_BINDING_BIT)) {
            continue;
          }
          queue_family_sparse_binding_ = j;
          queue_families_[j].queue_count =
              std::max(queue_families_[j].queue_count, uint32_t(1));
          break;
        }
      }
      // Don't expose, and disable during logical device creature, the sparse
      // binding feature if failed to obtain a queue supporting it.
      if (queue_family_sparse_binding_ == UINT32_MAX) {
        device_features_.sparseBinding = VK_FALSE;
      }
    }
    bool any_queue_potentially_supports_present = false;
    if (instance_extensions_.khr_surface) {
      // Request possible presentation queues.
      for (uint32_t j = 0; j < queue_family_count; ++j) {
#if XE_PLATFORM_WIN32
        if (instance_extensions_.khr_win32_surface &&
            !ifn_.vkGetPhysicalDeviceWin32PresentationSupportKHR(
                physical_device_current, j)) {
          continue;
        }
#endif
        any_queue_potentially_supports_present = true;
        QueueFamily& queue_family = queue_families_[j];
        queue_family.queue_count =
            std::max(queue_families_[j].queue_count, uint32_t(1));
        queue_family.potentially_supports_present = true;
      }
    }
    if (!any_queue_potentially_supports_present && is_surface_required_) {
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
      bool device_extensions_were_empty = !device_extension_count;
      device_extensions_enumerate_result =
          ifn_.vkEnumerateDeviceExtensionProperties(
              physical_device_current, nullptr, &device_extension_count,
              device_extensions_were_empty
                  ? nullptr
                  : device_extension_properties.data());
      // If the original extension count was 0 (first call), SUCCESS is
      // returned, not INCOMPLETE.
      if (device_extensions_enumerate_result == VK_SUCCESS ||
          device_extensions_enumerate_result == VK_INCOMPLETE) {
        device_extension_properties.resize(device_extension_count);
        if (device_extensions_enumerate_result == VK_SUCCESS &&
            (!device_extensions_were_empty || !device_extension_count)) {
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
    if (device_properties_.apiVersion >= VK_MAKE_API_VERSION(0, 1, 1, 0)) {
      device_extensions_.khr_dedicated_allocation = true;
      if (device_properties_.apiVersion >= VK_MAKE_API_VERSION(0, 1, 2, 0)) {
        device_extensions_.khr_image_format_list = true;
        device_extensions_.khr_shader_float_controls = true;
        device_extensions_.khr_spirv_1_4 = true;
      }
    }
    device_extensions_enabled.clear();
    for (const VkExtensionProperties& device_extension :
         device_extension_properties) {
      const char* device_extension_name = device_extension.extensionName;
      // Checking if already enabled as an optimization to do fewer and fewer
      // string comparisons, as well as to skip adding extensions promoted to
      // the core to device_extensions_enabled. Adding literals to
      // device_extensions_enabled for the most C string lifetime safety.
      if (!device_extensions_.ext_fragment_shader_interlock &&
          !std::strcmp(device_extension_name,
                       "VK_EXT_fragment_shader_interlock")) {
        device_extensions_enabled.push_back("VK_EXT_fragment_shader_interlock");
        device_extensions_.ext_fragment_shader_interlock = true;
      } else if (!device_extensions_.khr_dedicated_allocation &&
                 !std::strcmp(device_extension_name,
                              "VK_KHR_dedicated_allocation")) {
        device_extensions_enabled.push_back("VK_KHR_dedicated_allocation");
        device_extensions_.khr_dedicated_allocation = true;
      } else if (!device_extensions_.khr_image_format_list &&
                 !std::strcmp(device_extension_name,
                              "VK_KHR_image_format_list")) {
        device_extensions_enabled.push_back("VK_KHR_image_format_list");
        device_extensions_.khr_image_format_list = true;
      } else if (!device_extensions_.khr_shader_float_controls &&
                 !std::strcmp(device_extension_name,
                              "VK_KHR_shader_float_controls")) {
        device_extensions_enabled.push_back("VK_KHR_shader_float_controls");
        device_extensions_.khr_shader_float_controls = true;
      } else if (!device_extensions_.khr_spirv_1_4 &&
                 !std::strcmp(device_extension_name, "VK_KHR_spirv_1_4")) {
        device_extensions_enabled.push_back("VK_KHR_spirv_1_4");
        device_extensions_.khr_spirv_1_4 = true;
      } else if (!device_extensions_.khr_swapchain &&
                 !std::strcmp(device_extension_name, "VK_KHR_swapchain")) {
        device_extensions_enabled.push_back("VK_KHR_swapchain");
        device_extensions_.khr_swapchain = true;
      }
    }
    if (is_surface_required_ && !device_extensions_.khr_swapchain) {
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

  // Create the device.
  std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
  queue_create_infos.reserve(queue_families_.size());
  uint32_t used_queue_count = 0;
  uint32_t max_queue_count_per_family = 0;
  for (size_t i = 0; i < queue_families_.size(); ++i) {
    QueueFamily& queue_family = queue_families_[i];
    queue_family.queue_first_index = used_queue_count;
    if (!queue_family.queue_count) {
      continue;
    }
    VkDeviceQueueCreateInfo& queue_create_info =
        queue_create_infos.emplace_back();
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.pNext = nullptr;
    queue_create_info.flags = 0;
    queue_create_info.queueFamilyIndex = uint32_t(i);
    queue_create_info.queueCount = queue_family.queue_count;
    // pQueuePriorities will be set later based on max_queue_count_per_family.
    max_queue_count_per_family =
        std::max(max_queue_count_per_family, queue_family.queue_count);
    used_queue_count += queue_family.queue_count;
  }
  std::vector<float> queue_priorities;
  queue_priorities.resize(max_queue_count_per_family, 1.0f);
  for (VkDeviceQueueCreateInfo& queue_create_info : queue_create_infos) {
    queue_create_info.pQueuePriorities = queue_priorities.data();
  }
  VkDeviceCreateInfo device_create_info;
  device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_create_info.pNext = nullptr;
  device_create_info.flags = 0;
  device_create_info.queueCreateInfoCount = uint32_t(queue_create_infos.size());
  device_create_info.pQueueCreateInfos = queue_create_infos.data();
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
  std::memset(&dfn_, 0, sizeof(ifn_));
  bool device_functions_loaded = true;
#define XE_UI_VULKAN_FUNCTION(name)                                         \
  functions_loaded &=                                                       \
      (dfn_.name = PFN_##name(ifn_.vkGetDeviceProcAddr(device_, #name))) != \
      nullptr;
#define XE_UI_VULKAN_FUNCTION_DONT_PROMOTE(extension_name, core_name) \
  functions_loaded &=                                                 \
      (dfn_.extension_name = PFN_##extension_name(                    \
           ifn_.vkGetDeviceProcAddr(device_, #extension_name))) != nullptr;
#define XE_UI_VULKAN_FUNCTION_PROMOTE(extension_name, core_name) \
  functions_loaded &=                                            \
      (dfn_.extension_name = PFN_##extension_name(               \
           ifn_.vkGetDeviceProcAddr(device_, #core_name))) != nullptr;
  // Core - require unconditionally.
  {
    bool functions_loaded = true;
#include "xenia/ui/vulkan/functions/device_1_0.inc"
    if (!functions_loaded) {
      XELOGE("Failed to get Vulkan device function pointers");
      return false;
    }
  }
  // Extensions - disable the specific extension if failed to get its functions.
  if (device_extensions_.khr_swapchain) {
    bool functions_loaded = true;
#include "xenia/ui/vulkan/functions/device_khr_swapchain.inc"
    if (!functions_loaded) {
      // Outside the physical device selection loop, so can't just skip the
      // device anymore, but this shouldn't really happen anyway.
      XELOGE(
          "Failed to get Vulkan swapchain function pointers while swapchain "
          "support is required");
      return false;
    }
    device_extensions_.khr_swapchain = functions_loaded;
  }
#undef XE_UI_VULKAN_FUNCTION_PROMOTE
#undef XE_UI_VULKAN_FUNCTION_DONT_PROMOTE
#undef XE_UI_VULKAN_FUNCTION
  if (!device_functions_loaded) {
    XELOGE("Failed to get Vulkan device function pointers");
    return false;
  }

  // Report device information after verifying that extension function pointers
  // could be obtained.
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
  XELOGVK("* VK_KHR_image_format_list: {}",
          device_extensions_.khr_image_format_list ? "yes" : "no");
  XELOGVK("* VK_KHR_shader_float_controls: {}",
          device_extensions_.khr_shader_float_controls ? "yes" : "no");
  if (device_extensions_.khr_shader_float_controls) {
    XELOGVK(
        "  * Signed zero, inf, nan preserve for float32: {}",
        device_float_controls_properties_.shaderSignedZeroInfNanPreserveFloat32
            ? "yes"
            : "no");
    XELOGVK("  * Denorm flush to zero for float32: {}",
            device_float_controls_properties_.shaderDenormFlushToZeroFloat32
                ? "yes"
                : "no");
    XELOGVK("* VK_KHR_spirv_1_4: {}",
            device_extensions_.khr_spirv_1_4 ? "yes" : "no");
    XELOGVK("* VK_KHR_swapchain: {}",
            device_extensions_.khr_swapchain ? "yes" : "no");
  }
  // TODO(Triang3l): Report properties, features.

  // Get the queues.
  queues_.reset();
  queues_ = std::make_unique<Queue[]>(used_queue_count);
  uint32_t queue_index = 0;
  for (size_t i = 0; i < queue_families_.size(); ++i) {
    const QueueFamily& queue_family = queue_families_[i];
    if (!queue_family.queue_count) {
      continue;
    }
    assert_true(queue_index == queue_family.queue_first_index);
    for (uint32_t j = 0; j < queue_family.queue_count; ++j) {
      VkQueue queue;
      dfn_.vkGetDeviceQueue(device_, uint32_t(i), j, &queue);
      queues_[queue_index++].queue = queue;
    }
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

std::unique_ptr<Presenter> VulkanProvider::CreatePresenter(
    Presenter::HostGpuLossCallback host_gpu_loss_callback) {
  return VulkanPresenter::Create(host_gpu_loss_callback, *this);
}

std::unique_ptr<ImmediateDrawer> VulkanProvider::CreateImmediateDrawer() {
  return VulkanImmediateDrawer::Create(*this);
}

void VulkanProvider::SetDeviceObjectName(VkObjectType type, uint64_t handle,
                                         const char* name) const {
  if (!debug_names_used_) {
    return;
  }
  VkDebugUtilsObjectNameInfoEXT name_info;
  name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
  name_info.pNext = nullptr;
  name_info.objectType = type;
  name_info.objectHandle = handle;
  name_info.pObjectName = name;
  ifn_.vkSetDebugUtilsObjectNameEXT(device_, &name_info);
}

void VulkanProvider::AccumulateInstanceExtensions(
    size_t properties_count, const VkExtensionProperties* properties,
    bool request_debug_utils, InstanceExtensions& instance_extensions,
    std::vector<const char*>& instance_extensions_enabled) {
  for (size_t i = 0; i < properties_count; ++i) {
    const char* instance_extension_name = properties[i].extensionName;
    // Checking if already enabled as an optimization to do fewer and fewer
    // string comparisons, as well as to skip adding extensions promoted to the
    // core to instance_extensions_enabled. Adding literals to
    // instance_extensions_enabled for the most C string lifetime safety.
    if (request_debug_utils && !instance_extensions.ext_debug_utils &&
        !std::strcmp(instance_extension_name, "VK_EXT_debug_utils")) {
      // Debug utilities are only enabled when needed. Overhead in Xenia not
      // profiled, but better to avoid unless enabled by the user.
      instance_extensions_enabled.push_back("VK_EXT_debug_utils");
      instance_extensions.ext_debug_utils = true;
    } else if (!instance_extensions.khr_get_physical_device_properties2 &&
               !std::strcmp(instance_extension_name,
                            "VK_KHR_get_physical_device_properties2")) {
      instance_extensions_enabled.push_back(
          "VK_KHR_get_physical_device_properties2");
      instance_extensions.khr_get_physical_device_properties2 = true;
    } else if (!instance_extensions.khr_surface &&
               !std::strcmp(instance_extension_name, "VK_KHR_surface")) {
      instance_extensions_enabled.push_back("VK_KHR_surface");
      instance_extensions.khr_surface = true;
    } else {
#if XE_PLATFORM_ANDROID
      if (!instance_extensions.khr_android_surface &&
          !std::strcmp(instance_extension_name, "VK_KHR_android_surface")) {
        instance_extensions_enabled.push_back("VK_KHR_android_surface");
        instance_extensions.khr_android_surface = true;
      }
#elif XE_PLATFORM_GNU_LINUX
      if (!instance_extensions.khr_xcb_surface &&
          !std::strcmp(instance_extension_name, "VK_KHR_xcb_surface")) {
        instance_extensions_enabled.push_back("VK_KHR_xcb_surface");
        instance_extensions.khr_xcb_surface = true;
      }
#elif XE_PLATFORM_WIN32
      if (!instance_extensions.khr_win32_surface &&
          !std::strcmp(instance_extension_name, "VK_KHR_win32_surface")) {
        instance_extensions_enabled.push_back("VK_KHR_win32_surface");
        instance_extensions.khr_win32_surface = true;
      }
#endif
    }
  }
}

VkBool32 VKAPI_CALL VulkanProvider::DebugMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_types,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data) {
  const char* severity_string;
  switch (message_severity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
      severity_string = "verbose output";
      break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
      severity_string = "info";
      break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
      severity_string = "warning";
      break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
      severity_string = "error";
      break;
    default:
      switch (xe::bit_count(uint32_t(message_severity))) {
        case 0:
          severity_string = "no-severity";
          break;
        case 1:
          severity_string = "unknown-severity";
          break;
        default:
          severity_string = "multi-severity";
      }
  }
  const char* type_string;
  switch (message_types) {
    case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
      type_string = "general";
      break;
    case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
      type_string = "validation";
      break;
    case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
      type_string = "performance";
      break;
    default:
      switch (xe::bit_count(uint32_t(message_types))) {
        case 0:
          type_string = "no-type";
          break;
        case 1:
          type_string = "unknown-type";
          break;
        default:
          type_string = "multi-type";
      }
  }
  XELOGVK("Vulkan {} {}: {}", type_string, severity_string,
          callback_data->pMessage);
  return VK_FALSE;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
