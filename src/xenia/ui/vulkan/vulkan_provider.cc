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
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>
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

DEFINE_bool(
    vulkan_validation, false,
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
  // TODO(Triang3l): Enumerate portability subset devices using
  // VK_KHR_portability_enumeration when ready.
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
#define XE_UI_VULKAN_FUNCTION_DONT_PROMOTE(extension_name, core_name) \
  functions_loaded &=                                                 \
      (ifn_.core_name = PFN_##core_name(lfn_.vkGetInstanceProcAddr(   \
           instance_, #extension_name))) != nullptr;
#define XE_UI_VULKAN_FUNCTION_PROMOTE(extension_name, core_name) \
  functions_loaded &=                                            \
      (ifn_.core_name = PFN_##core_name(                         \
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
  for (size_t i = physical_device_index_first; i <= physical_device_index_last;
       ++i) {
    physical_device_ = physical_devices[i];
    TryCreateDevice();
    if (device_ != VK_NULL_HANDLE) {
      break;
    }
  }
  if (device_ == VK_NULL_HANDLE) {
    XELOGE("Failed to select a compatible Vulkan physical device");
    physical_device_ = VK_NULL_HANDLE;
    return false;
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

void VulkanProvider::TryCreateDevice() {
  assert_true(physical_device_ != VK_NULL_HANDLE);
  assert_true(device_ == VK_NULL_HANDLE);

  static_assert(std::is_trivially_copyable_v<DeviceInfo>,
                "DeviceInfo must be safe to clear using memset");
  std::memset(&device_info_, 0, sizeof(device_info_));

  VkDeviceCreateInfo device_create_info = {
      VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};

  // Needed device extensions, properties and features.

  VkPhysicalDeviceProperties properties;
  ifn_.vkGetPhysicalDeviceProperties(physical_device_, &properties);

  XELOGVK("Trying Vulkan device '{}'", properties.deviceName);

  if (!instance_extensions_.khr_get_physical_device_properties2) {
    // Many extensions promoted to Vulkan 1.1 and newer require the instance
    // extension VK_KHR_get_physical_device_properties2, which is itself in the
    // core 1.0, although there's one instance for all physical devices.
    properties.apiVersion = VK_MAKE_API_VERSION(
        0, 1, 0, VK_API_VERSION_PATCH(properties.apiVersion));
  }

  device_info_.apiVersion = properties.apiVersion;

  XELOGVK("Device Vulkan API version: {}.{}.{}",
          VK_API_VERSION_MAJOR(properties.apiVersion),
          VK_API_VERSION_MINOR(properties.apiVersion),
          VK_API_VERSION_PATCH(properties.apiVersion));

  std::vector<VkExtensionProperties> extension_properties;
  VkResult extensions_enumerate_result;
  for (;;) {
    uint32_t extension_count = uint32_t(extension_properties.size());
    bool extensions_were_empty = !extension_count;
    extensions_enumerate_result = ifn_.vkEnumerateDeviceExtensionProperties(
        physical_device_, nullptr, &extension_count,
        extensions_were_empty ? nullptr : extension_properties.data());
    // If the original extension count was 0 (first call), SUCCESS is
    // returned, not INCOMPLETE.
    if (extensions_enumerate_result == VK_SUCCESS ||
        extensions_enumerate_result == VK_INCOMPLETE) {
      extension_properties.resize(extension_count);
      if (extensions_enumerate_result == VK_SUCCESS &&
          (!extensions_were_empty || !extension_count)) {
        break;
      }
    } else {
      break;
    }
  }
  if (extensions_enumerate_result != VK_SUCCESS) {
    XELOGE("Failed to query Vulkan device '{}' extensions",
           properties.deviceName);
    return;
  }

  XELOGVK("Requested Vulkan device extensions:");

  std::vector<const char*> enabled_extensions;

  if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 1, 0)) {
    device_info_.ext_1_1_VK_KHR_dedicated_allocation = true;
    device_info_.ext_1_1_VK_KHR_get_memory_requirements2 = true;
    device_info_.ext_1_1_VK_KHR_sampler_ycbcr_conversion = true;
    device_info_.ext_1_1_VK_KHR_bind_memory2 = true;
  }
  if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 2, 0)) {
    device_info_.ext_1_2_VK_KHR_sampler_mirror_clamp_to_edge = true;
    device_info_.ext_1_2_VK_KHR_image_format_list = true;
    device_info_.ext_1_2_VK_KHR_shader_float_controls = true;
    device_info_.ext_1_2_VK_KHR_spirv_1_4 = true;
  }
  if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 3, 0)) {
    device_info_.ext_1_3_VK_EXT_shader_demote_to_helper_invocation = true;
    device_info_.ext_1_3_VK_KHR_maintenance4 = true;
  }

  for (const VkExtensionProperties& extension : extension_properties) {
    // Checking if already enabled as an optimization to do fewer and fewer
    // string comparisons.
#define EXTENSION(name)                               \
  if (!device_info_.ext_##name &&                     \
      !std::strcmp(extension.extensionName, #name)) { \
    enabled_extensions.push_back(#name);              \
    device_info_.ext_##name = true;                   \
    XELOGVK("* " #name);                              \
  }
#define EXTENSION_PROMOTED(name, minor_version)         \
  if (!device_info_.ext_1_##minor_version##_##name &&   \
      !std::strcmp(extension.extensionName, #name)) {   \
    enabled_extensions.push_back(#name);                \
    device_info_.ext_1_##minor_version##_##name = true; \
    XELOGVK("* " #name);                                \
  }
    EXTENSION(VK_KHR_swapchain)
    EXTENSION(VK_EXT_shader_stencil_export)
    if (instance_extensions_.khr_get_physical_device_properties2) {
      EXTENSION(VK_KHR_portability_subset)
      EXTENSION(VK_EXT_memory_budget)
      EXTENSION(VK_EXT_fragment_shader_interlock)
      EXTENSION(VK_EXT_non_seamless_cube_map)
    } else {
      if (!std::strcmp(extension.extensionName, "VK_KHR_portability_subset")) {
        XELOGW(
            "Vulkan device '{}' is a portability subset device, but its "
            "portability subset features can't be queried as the instance "
            "doesn't support VK_KHR_get_physical_device_properties2",
            properties.deviceName);
        return;
      }
    }
    if (properties.apiVersion < VK_MAKE_API_VERSION(0, 1, 1, 0)) {
      EXTENSION_PROMOTED(VK_KHR_dedicated_allocation, 1)
      EXTENSION_PROMOTED(VK_KHR_get_memory_requirements2, 1)
      EXTENSION_PROMOTED(VK_KHR_bind_memory2, 1)
      if (instance_extensions_.khr_get_physical_device_properties2) {
        EXTENSION_PROMOTED(VK_KHR_sampler_ycbcr_conversion, 1)
      }
    }
    if (properties.apiVersion < VK_MAKE_API_VERSION(0, 1, 2, 0)) {
      EXTENSION_PROMOTED(VK_KHR_sampler_mirror_clamp_to_edge, 2)
      EXTENSION_PROMOTED(VK_KHR_image_format_list, 2)
      if (instance_extensions_.khr_get_physical_device_properties2) {
        EXTENSION_PROMOTED(VK_KHR_shader_float_controls, 2)
      }
      if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 1, 0)) {
        EXTENSION_PROMOTED(VK_KHR_spirv_1_4, 2)
      }
    }
    if (properties.apiVersion < VK_MAKE_API_VERSION(0, 1, 3, 0)) {
      if (instance_extensions_.khr_get_physical_device_properties2) {
        EXTENSION_PROMOTED(VK_EXT_shader_demote_to_helper_invocation, 3)
      }
      if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 1, 0)) {
        EXTENSION_PROMOTED(VK_KHR_maintenance4, 3)
      }
    }
#undef EXTENSION_PROMOTED
#undef EXTENSION
  }

  if (is_surface_required_ && !device_info_.ext_VK_KHR_swapchain) {
    XELOGVK("Vulkan device '{}' doesn't support presentation",
            properties.deviceName);
    return;
  }

  XELOGVK("Requested properties and features of the Vulkan device:");

  XELOGVK("* driverVersion: 0x{:X}", properties.driverVersion);
  XELOGVK("* vendorID: 0x{:04X}", properties.vendorID);
  XELOGVK("* deviceID: 0x{:04X}", properties.deviceID);

#define LIMIT(name)                           \
  device_info_.name = properties.limits.name; \
  XELOGVK("* " #name ": {}", properties.limits.name);
#define LIMIT_SAMPLE_COUNTS(name)             \
  device_info_.name = properties.limits.name; \
  XELOGVK("* " #name ": 0b{:b}", static_cast<uint32_t>(properties.limits.name));
  LIMIT(maxImageDimension2D)
  LIMIT(maxImageDimension3D)
  LIMIT(maxImageDimensionCube)
  LIMIT(maxImageArrayLayers)
  LIMIT(maxStorageBufferRange)
  LIMIT(maxSamplerAllocationCount)
  LIMIT(maxPerStageDescriptorSamplers)
  LIMIT(maxPerStageDescriptorStorageBuffers)
  LIMIT(maxPerStageDescriptorSampledImages)
  LIMIT(maxPerStageResources)
  LIMIT(maxVertexOutputComponents)
  LIMIT(maxTessellationEvaluationOutputComponents)
  LIMIT(maxGeometryInputComponents)
  LIMIT(maxGeometryOutputComponents)
  LIMIT(maxGeometryTotalOutputComponents)
  LIMIT(maxFragmentInputComponents)
  LIMIT(maxFragmentCombinedOutputResources)
  LIMIT(maxSamplerAnisotropy)
  std::memcpy(device_info_.maxViewportDimensions,
              properties.limits.maxViewportDimensions,
              sizeof(device_info_.maxViewportDimensions));
  XELOGVK("* maxViewportDimensions: {} x {}",
          properties.limits.maxViewportDimensions[0],
          properties.limits.maxViewportDimensions[1]);
  std::memcpy(device_info_.viewportBoundsRange,
              properties.limits.viewportBoundsRange,
              sizeof(device_info_.viewportBoundsRange));
  XELOGVK("* viewportBoundsRange: [{}, {}]",
          properties.limits.viewportBoundsRange[0],
          properties.limits.viewportBoundsRange[1]);
  LIMIT(minUniformBufferOffsetAlignment)
  LIMIT(minStorageBufferOffsetAlignment)
  LIMIT(maxFramebufferWidth)
  LIMIT(maxFramebufferHeight)
  LIMIT_SAMPLE_COUNTS(framebufferColorSampleCounts)
  LIMIT_SAMPLE_COUNTS(framebufferDepthSampleCounts)
  LIMIT_SAMPLE_COUNTS(framebufferStencilSampleCounts)
  LIMIT_SAMPLE_COUNTS(framebufferNoAttachmentsSampleCounts)
  LIMIT_SAMPLE_COUNTS(sampledImageColorSampleCounts)
  LIMIT_SAMPLE_COUNTS(sampledImageIntegerSampleCounts)
  LIMIT_SAMPLE_COUNTS(sampledImageDepthSampleCounts)
  LIMIT_SAMPLE_COUNTS(sampledImageStencilSampleCounts)
  LIMIT(standardSampleLocations)
  LIMIT(optimalBufferCopyOffsetAlignment)
  LIMIT(optimalBufferCopyRowPitchAlignment)
  LIMIT(nonCoherentAtomSize)
#undef LIMIT_SAMPLE_COUNTS
#undef LIMIT

  VkPhysicalDeviceFeatures supported_features;
  ifn_.vkGetPhysicalDeviceFeatures(physical_device_, &supported_features);
  // Enabling only needed features because drivers may take more optimal paths
  // when certain features are disabled. Also, in VK_EXT_shader_object, which
  // features are enabled effects the pipeline state must be set before drawing.
  VkPhysicalDeviceFeatures enabled_features = {};

#define FEATURE(name)                \
  if (supported_features.name) {     \
    device_info_.name = true;        \
    enabled_features.name = VK_TRUE; \
    XELOGVK("* " #name);             \
  }
  FEATURE(fullDrawIndexUint32)
  FEATURE(independentBlend)
  FEATURE(geometryShader)
  FEATURE(tessellationShader)
  FEATURE(sampleRateShading)
  FEATURE(depthClamp)
  FEATURE(fillModeNonSolid)
  FEATURE(samplerAnisotropy)
  FEATURE(vertexPipelineStoresAndAtomics)
  FEATURE(fragmentStoresAndAtomics)
  FEATURE(shaderClipDistance)
  FEATURE(shaderCullDistance)
  FEATURE(sparseBinding)
  FEATURE(sparseResidencyBuffer)
#undef FEATURE

  VkPhysicalDeviceProperties2 properties2 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
#define PROPERTIES2_DECLARE(type_suffix, structure_type_suffix) \
  VkPhysicalDevice##type_suffix supported_##type_suffix = {     \
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_##structure_type_suffix};
#define PROPERTIES2_ADD(type_suffix)                   \
  (supported_##type_suffix).pNext = properties2.pNext; \
  properties2.pNext = &(supported_##type_suffix);

  VkPhysicalDeviceFeatures2 features2 = {
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
#define FEATURES2_DECLARE(type_suffix, structure_type_suffix)     \
  VkPhysicalDevice##type_suffix supported_##type_suffix = {       \
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_##structure_type_suffix}; \
  VkPhysicalDevice##type_suffix enabled_##type_suffix = {         \
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_##structure_type_suffix};
#define FEATURES2_ADD(type_suffix)                                             \
  (supported_##type_suffix).pNext = features2.pNext;                           \
  features2.pNext = &(supported_##type_suffix);                                \
  (enabled_##type_suffix).pNext = const_cast<void*>(device_create_info.pNext); \
  device_create_info.pNext = &(enabled_##type_suffix);
  // VUID-VkDeviceCreateInfo-pNext: "If the pNext chain includes a
  // VkPhysicalDeviceVulkan1XFeatures structure, then it must not include..."
  // Enabling the features in Vulkan1XFeatures instead.
#define FEATURES2_ADD_PROMOTED(type_suffix, minor_version)                   \
  (supported_##type_suffix).pNext = features2.pNext;                         \
  features2.pNext = &(supported_##type_suffix);                              \
  if (properties.apiVersion < VK_MAKE_API_VERSION(0, 1, minor_version, 0)) { \
    (enabled_##type_suffix).pNext =                                          \
        const_cast<void*>(device_create_info.pNext);                         \
    device_create_info.pNext = &(enabled_##type_suffix);                     \
  }

  FEATURES2_DECLARE(Vulkan11Features, VULKAN_1_1_FEATURES)
  if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 1, 0)) {
    FEATURES2_ADD(Vulkan11Features)
  }
  FEATURES2_DECLARE(Vulkan12Features, VULKAN_1_2_FEATURES)
  if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 2, 0)) {
    FEATURES2_ADD(Vulkan12Features)
  }
  FEATURES2_DECLARE(Vulkan13Features, VULKAN_1_3_FEATURES)
  if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 3, 0)) {
    FEATURES2_ADD(Vulkan13Features)
  }

  FEATURES2_DECLARE(PortabilitySubsetFeaturesKHR,
                    PORTABILITY_SUBSET_FEATURES_KHR)
  if (device_info_.ext_VK_KHR_portability_subset) {
    FEATURES2_ADD(PortabilitySubsetFeaturesKHR)
  }
  PROPERTIES2_DECLARE(FloatControlsProperties, FLOAT_CONTROLS_PROPERTIES)
  if (device_info_.ext_1_2_VK_KHR_shader_float_controls) {
    PROPERTIES2_ADD(FloatControlsProperties)
  }
  FEATURES2_DECLARE(FragmentShaderInterlockFeaturesEXT,
                    FRAGMENT_SHADER_INTERLOCK_FEATURES_EXT)
  if (device_info_.ext_VK_EXT_fragment_shader_interlock) {
    FEATURES2_ADD(FragmentShaderInterlockFeaturesEXT)
  }
  FEATURES2_DECLARE(ShaderDemoteToHelperInvocationFeatures,
                    SHADER_DEMOTE_TO_HELPER_INVOCATION_FEATURES)
  if (device_info_.ext_1_3_VK_EXT_shader_demote_to_helper_invocation) {
    FEATURES2_ADD_PROMOTED(ShaderDemoteToHelperInvocationFeatures, 3)
  }
  FEATURES2_DECLARE(NonSeamlessCubeMapFeaturesEXT,
                    NON_SEAMLESS_CUBE_MAP_FEATURES_EXT)
  if (device_info_.ext_VK_EXT_non_seamless_cube_map) {
    FEATURES2_ADD(NonSeamlessCubeMapFeaturesEXT)
  }

  if (instance_extensions_.khr_get_physical_device_properties2) {
    ifn_.vkGetPhysicalDeviceProperties2(physical_device_, &properties2);
    ifn_.vkGetPhysicalDeviceFeatures2(physical_device_, &features2);
  }

#undef FEATURES2_ADD_PROMOTED
#undef FEATURES2_ADD
#undef FEATURES2_DECLARE
#undef PROPERTIES2_ADD
#undef PROPERTIES2_DECLARE

  // VK_KHR_portability_subset removes functionality rather than adding it, so
  // if the extension is not present, all its features are true by default.
#define PORTABILITY_SUBSET_FEATURE(name)                   \
  if (device_info_.ext_VK_KHR_portability_subset) {        \
    if (supported_PortabilitySubsetFeaturesKHR.name) {     \
      device_info_.name = true;                            \
      enabled_PortabilitySubsetFeaturesKHR.name = VK_TRUE; \
      XELOGVK("* " #name);                                 \
    }                                                      \
  } else {                                                 \
    device_info_.name = true;                              \
  }
  PORTABILITY_SUBSET_FEATURE(constantAlphaColorBlendFactors)
  PORTABILITY_SUBSET_FEATURE(imageViewFormatReinterpretation)
  PORTABILITY_SUBSET_FEATURE(imageViewFormatSwizzle)
  PORTABILITY_SUBSET_FEATURE(pointPolygons)
  PORTABILITY_SUBSET_FEATURE(separateStencilMaskRef)
  PORTABILITY_SUBSET_FEATURE(shaderSampleRateInterpolationFunctions)
  PORTABILITY_SUBSET_FEATURE(triangleFans)
#undef PORTABILITY_SUBSET_FEATURE

#define EXTENSION_PROPERTY(type_suffix, name)       \
  device_info_.name = supported_##type_suffix.name; \
  XELOGVK("* " #name ": {}", supported_##type_suffix.name);
#define EXTENSION_FEATURE(type_suffix, name) \
  if (supported_##type_suffix.name) {        \
    device_info_.name = true;                \
    enabled_##type_suffix.name = VK_TRUE;    \
    XELOGVK("* " #name);                     \
  }
#define EXTENSION_FEATURE_PROMOTED(type_suffix, name, minor_version) \
  if (supported_##type_suffix.name) {                                \
    device_info_.name = true;                                        \
    enabled_##type_suffix.name = VK_TRUE;                            \
    enabled_Vulkan1##minor_version##Features.name = VK_TRUE;         \
    XELOGVK("* " #name);                                             \
  }
#define EXTENSION_FEATURE_PROMOTED_AS_OPTIONAL(name, minor_version) \
  if (supported_Vulkan1##minor_version##Features.name) {            \
    device_info_.name = true;                                       \
    enabled_Vulkan1##minor_version##Features.name = VK_TRUE;        \
    XELOGVK("* " #name);                                            \
  }

  if (device_info_.ext_1_2_VK_KHR_sampler_mirror_clamp_to_edge) {
    if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 2, 0)) {
      // Promoted - the feature is optional, and must be enabled if the
      // extension is also enabled
      // (VUID-VkDeviceCreateInfo-ppEnabledExtensionNames-02832).
      EXTENSION_FEATURE_PROMOTED_AS_OPTIONAL(samplerMirrorClampToEdge, 2)
    } else {
      // Extension - the feature is implied.
      device_info_.samplerMirrorClampToEdge = true;
      XELOGVK("* samplerMirrorClampToEdge");
    }
  }

  if (device_info_.ext_1_2_VK_KHR_shader_float_controls) {
    EXTENSION_PROPERTY(FloatControlsProperties,
                       shaderSignedZeroInfNanPreserveFloat32)
    EXTENSION_PROPERTY(FloatControlsProperties, shaderDenormFlushToZeroFloat32)
    EXTENSION_PROPERTY(FloatControlsProperties, shaderRoundingModeRTEFloat32)
  }

  if (device_info_.ext_VK_EXT_fragment_shader_interlock) {
    EXTENSION_FEATURE(FragmentShaderInterlockFeaturesEXT,
                      fragmentShaderSampleInterlock)
    // fragmentShaderPixelInterlock is not needed by Xenia if
    // fragmentShaderSampleInterlock is available as it accesses only per-sample
    // data.
    if (!device_info_.fragmentShaderSampleInterlock) {
      EXTENSION_FEATURE(FragmentShaderInterlockFeaturesEXT,
                        fragmentShaderPixelInterlock)
    }
  }

  if (device_info_.ext_1_3_VK_EXT_shader_demote_to_helper_invocation) {
    EXTENSION_FEATURE_PROMOTED(ShaderDemoteToHelperInvocationFeatures,
                               shaderDemoteToHelperInvocation, 3)
  }

  if (device_info_.ext_VK_EXT_non_seamless_cube_map) {
    EXTENSION_FEATURE(NonSeamlessCubeMapFeaturesEXT, nonSeamlessCubeMap)
  }

#undef EXTENSION_FEATURE_PROMOTED_AS_OPTIONAL
#undef EXTENSION_FEATURE_PROMOTED
#undef EXTENSION_FEATURE
#undef EXTENSION_PROPERTY

  // Memory types.

  VkPhysicalDeviceMemoryProperties memory_properties;
  ifn_.vkGetPhysicalDeviceMemoryProperties(physical_device_,
                                           &memory_properties);
  for (uint32_t memory_type_index = 0;
       memory_type_index < memory_properties.memoryTypeCount;
       ++memory_type_index) {
    VkMemoryPropertyFlags memory_property_flags =
        memory_properties.memoryTypes[memory_type_index].propertyFlags;
    uint32_t memory_type_bit = uint32_t(1) << memory_type_index;
    if (memory_property_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
      device_info_.memory_types_device_local |= memory_type_bit;
    }
    if (memory_property_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
      device_info_.memory_types_host_visible |= memory_type_bit;
    }
    if (memory_property_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
      device_info_.memory_types_host_coherent |= memory_type_bit;
    }
    if (memory_property_flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
      device_info_.memory_types_host_cached |= memory_type_bit;
    }
  }

  // Queue families.

  uint32_t queue_family_count;
  ifn_.vkGetPhysicalDeviceQueueFamilyProperties(physical_device_,
                                                &queue_family_count, nullptr);
  std::vector<VkQueueFamilyProperties> queue_families_properties(
      queue_family_count);
  ifn_.vkGetPhysicalDeviceQueueFamilyProperties(
      physical_device_, &queue_family_count, queue_families_properties.data());
  queue_families_properties.resize(queue_family_count);

  queue_families_.clear();
  queue_families_.resize(queue_family_count);

  queue_family_graphics_compute_ = UINT32_MAX;
  queue_family_sparse_binding_ = UINT32_MAX;
  if (device_info_.sparseBinding) {
    // Prefer a queue family that supports both graphics/compute and sparse
    // binding because in Xenia sparse binding is done serially with graphics
    // work.
    for (uint32_t queue_family_index = 0;
         queue_family_index < queue_family_count; ++queue_family_index) {
      VkQueueFlags queue_flags =
          queue_families_properties[queue_family_index].queueFlags;
      bool is_graphics_compute =
          (queue_flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
          (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
      bool is_sparse_binding = (queue_flags & VK_QUEUE_SPARSE_BINDING_BIT) ==
                               VK_QUEUE_SPARSE_BINDING_BIT;
      if (is_graphics_compute && is_sparse_binding) {
        queue_family_graphics_compute_ = queue_family_index;
        queue_family_sparse_binding_ = queue_family_index;
        break;
      }
      // If can't do both, prefer the queue family that can do either with the
      // lowest index.
      if (is_graphics_compute && queue_family_graphics_compute_ == UINT32_MAX) {
        queue_family_graphics_compute_ = queue_family_index;
      }
      if (is_sparse_binding && queue_family_sparse_binding_ == UINT32_MAX) {
        queue_family_sparse_binding_ = queue_family_index;
      }
    }
  } else {
    for (uint32_t queue_family_index = 0;
         queue_family_index < queue_family_count; ++queue_family_index) {
      if ((queue_families_properties[queue_family_index].queueFlags &
           (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
          (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
        queue_family_graphics_compute_ = queue_family_index;
        break;
      }
    }
  }

  if (queue_family_graphics_compute_ == UINT32_MAX) {
    XELOGVK("Vulkan device '{}' doesn't have a graphics and compute queue",
            properties.deviceName);
    return;
  }
  queue_families_[queue_family_graphics_compute_].queue_count = std::max(
      uint32_t(1), queue_families_[queue_family_graphics_compute_].queue_count);

  if (device_info_.sparseBinding &&
      queue_family_sparse_binding_ == UINT32_MAX) {
    XELOGVK(
        "Vulkan device '{}' reports that it supports sparse binding, but "
        "doesn't have a queue that can perform sparse binding operations, "
        "disabling sparse binding",
        properties.deviceName);
    device_info_.sparseBinding = false;
    enabled_features.sparseBinding = false;
  }
  if (!enabled_features.sparseBinding) {
    device_info_.sparseResidencyBuffer = false;
    enabled_features.sparseResidencyBuffer = false;
  }
  if (queue_family_sparse_binding_ != UINT32_MAX) {
    queue_families_[queue_family_sparse_binding_].queue_count = std::max(
        uint32_t(1), queue_families_[queue_family_sparse_binding_].queue_count);
  }

  // Request queues of all families potentially supporting presentation as which
  // ones will actually be used depends on the surface object.
  bool any_queue_potentially_supports_present = false;
  if (instance_extensions_.khr_surface) {
    for (uint32_t queue_family_index = 0;
         queue_family_index < queue_family_count; ++queue_family_index) {
#if XE_PLATFORM_WIN32
      if (instance_extensions_.khr_win32_surface &&
          !ifn_.vkGetPhysicalDeviceWin32PresentationSupportKHR(
              physical_device_, queue_family_index)) {
        continue;
      }
#endif
      QueueFamily& queue_family = queue_families_[queue_family_index];
      // Requesting an additional queue in each family where possible so
      // asynchronous presentation can potentially be done within a single queue
      // family too.
      queue_family.queue_count =
          std::min(queue_families_properties[queue_family_index].queueCount,
                   queue_family.queue_count + uint32_t(1));
      queue_family.potentially_supports_present = true;
      any_queue_potentially_supports_present = true;
    }
  }
  if (!any_queue_potentially_supports_present && is_surface_required_) {
    XELOGVK(
        "Vulkan device '{}' doesn't have any queues supporting presentation",
        properties.deviceName);
    return;
  }

  std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
  queue_create_infos.reserve(queue_families_.size());
  uint32_t used_queue_count = 0;
  uint32_t max_queue_count_per_family = 0;
  for (uint32_t queue_family_index = 0; queue_family_index < queue_family_count;
       ++queue_family_index) {
    QueueFamily& queue_family = queue_families_[queue_family_index];
    queue_family.queue_first_index = used_queue_count;
    if (!queue_family.queue_count) {
      continue;
    }
    VkDeviceQueueCreateInfo& queue_create_info =
        queue_create_infos.emplace_back();
    queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.pNext = nullptr;
    queue_create_info.flags = 0;
    queue_create_info.queueFamilyIndex = queue_family_index;
    queue_create_info.queueCount = queue_family.queue_count;
    // pQueuePriorities will be set later based on max_queue_count_per_family.
    max_queue_count_per_family =
        std::max(max_queue_count_per_family, queue_family.queue_count);
    used_queue_count += queue_family.queue_count;
  }
  std::vector<float> queue_priorities(max_queue_count_per_family, 1.0f);
  for (VkDeviceQueueCreateInfo& queue_create_info : queue_create_infos) {
    queue_create_info.pQueuePriorities = queue_priorities.data();
  }

  // Create the device.

  device_create_info.queueCreateInfoCount =
      static_cast<uint32_t>(queue_create_infos.size());
  device_create_info.pQueueCreateInfos = queue_create_infos.data();
  device_create_info.enabledExtensionCount =
      static_cast<uint32_t>(enabled_extensions.size());
  device_create_info.ppEnabledExtensionNames = enabled_extensions.data();
  device_create_info.pEnabledFeatures = &enabled_features;
  VkResult create_device_result = ifn_.vkCreateDevice(
      physical_device_, &device_create_info, nullptr, &device_);
  if (create_device_result != VK_SUCCESS) {
    XELOGE(
        "Failed to create a Vulkan device for physical device '{}', result {}",
        properties.deviceName, static_cast<int32_t>(create_device_result));
    device_ = VK_NULL_HANDLE;
    return;
  }

  // Device function pointers.

  std::memset(&dfn_, 0, sizeof(dfn_));

  bool functions_loaded = true;

#define XE_UI_VULKAN_FUNCTION(name)                                         \
  functions_loaded &=                                                       \
      (dfn_.name = PFN_##name(ifn_.vkGetDeviceProcAddr(device_, #name))) != \
      nullptr;

  // Vulkan 1.0.
#include "xenia/ui/vulkan/functions/device_1_0.inc"

  // Promoted extensions when the API version they're promoted to is supported.
#define XE_UI_VULKAN_FUNCTION_PROMOTED(extension_name, core_name) \
  functions_loaded &=                                             \
      (dfn_.core_name = PFN_##core_name(                          \
           ifn_.vkGetDeviceProcAddr(device_, #core_name))) != nullptr;
  if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 1, 0)) {
#include "xenia/ui/vulkan/functions/device_khr_bind_memory2.inc"
#include "xenia/ui/vulkan/functions/device_khr_get_memory_requirements2.inc"
  }
  if (properties.apiVersion >= VK_MAKE_API_VERSION(0, 1, 3, 0)) {
#include "xenia/ui/vulkan/functions/device_khr_maintenance4.inc"
  }
#undef XE_UI_VULKAN_FUNCTION_PROMOTED

  // Non-promoted extensions, and promoted extensions on API versions lower than
  // the ones they were promoted to.
#define XE_UI_VULKAN_FUNCTION_PROMOTED(extension_name, core_name) \
  functions_loaded &=                                             \
      (dfn_.core_name = PFN_##core_name(                          \
           ifn_.vkGetDeviceProcAddr(device_, #extension_name))) != nullptr;
  if (device_info_.ext_VK_KHR_swapchain) {
#include "xenia/ui/vulkan/functions/device_khr_swapchain.inc"
  }
  if (properties.apiVersion < VK_MAKE_API_VERSION(0, 1, 1, 0)) {
    if (device_info_.ext_1_1_VK_KHR_get_memory_requirements2) {
#include "xenia/ui/vulkan/functions/device_khr_get_memory_requirements2.inc"
    }
    if (device_info_.ext_1_1_VK_KHR_bind_memory2) {
#include "xenia/ui/vulkan/functions/device_khr_bind_memory2.inc"
    }
  }
  if (properties.apiVersion < VK_MAKE_API_VERSION(0, 1, 3, 0)) {
    if (device_info_.ext_1_3_VK_KHR_maintenance4) {
#include "xenia/ui/vulkan/functions/device_khr_maintenance4.inc"
    }
  }
#undef XE_UI_VULKAN_FUNCTION_PROMOTED

#undef XE_UI_VULKAN_FUNCTION

  if (!functions_loaded) {
    XELOGE("Failed to get all Vulkan device function pointers for '{}'",
           properties.deviceName);
    ifn_.vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
    return;
  }

  // Queues.

  queues_.reset();
  queues_ = std::make_unique<Queue[]>(used_queue_count);
  uint32_t queue_index = 0;
  for (uint32_t queue_family_index = 0; queue_family_index < queue_family_count;
       ++queue_family_index) {
    const QueueFamily& queue_family = queue_families_[queue_family_index];
    if (!queue_family.queue_count) {
      continue;
    }
    assert_true(queue_index == queue_family.queue_first_index);
    for (uint32_t family_queue_index = 0;
         family_queue_index < queue_family.queue_count; ++family_queue_index) {
      VkQueue queue;
      dfn_.vkGetDeviceQueue(device_, queue_family_index, family_queue_index,
                            &queue);
      queues_[queue_index++].queue = queue;
    }
  }

  XELOGVK("Created a Vulkan device for physical device '{}'",
          properties.deviceName);
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
