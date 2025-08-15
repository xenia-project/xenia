/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_instance.h"

#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/ui/vulkan/vulkan_presenter.h"

#if XE_PLATFORM_LINUX
#include <dlfcn.h>
#elif XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

DEFINE_bool(
    vulkan_log_debug_messages, true,
    "Write Vulkan VK_EXT_debug_utils messages to the Xenia log, as opposed to "
    "the OS debug output.",
    "Vulkan");

namespace xe {
namespace ui {
namespace vulkan {

std::unique_ptr<VulkanInstance> VulkanInstance::Create(
    const bool with_surface, const bool try_enable_validation) {
  std::unique_ptr<VulkanInstance> vulkan_instance(new VulkanInstance());

  // Load the RenderDoc API if connected.

  vulkan_instance->renderdoc_api_ = RenderDocAPI::CreateIfConnected();

  // Load the loader library.

  Functions& ifn = vulkan_instance->functions_;

  bool functions_loaded = true;
#if XE_PLATFORM_LINUX
#if XE_PLATFORM_ANDROID
  const char* const loader_library_name = "libvulkan.so";
#else
  const char* const loader_library_name = "libvulkan.so.1";
#endif
  // http://developer.download.nvidia.com/mobile/shield/assets/Vulkan/UsingtheVulkanAPI.pdf
  vulkan_instance->loader_ = dlopen(loader_library_name, RTLD_NOW | RTLD_LOCAL);
  if (!vulkan_instance->loader_) {
    XELOGE("Failed to load {}", loader_library_name);
    return nullptr;
  }
#define XE_VULKAN_LOAD_LOADER_FUNCTION(name)                             \
  functions_loaded &=                                                    \
      (ifn.name = PFN_##name(dlsym(vulkan_instance->loader_, #name))) != \
      nullptr;
#elif XE_PLATFORM_WIN32
  vulkan_instance->loader_ = LoadLibraryW(L"vulkan-1.dll");
  if (!vulkan_instance->loader_) {
    XELOGE("Failed to load vulkan-1.dll");
    return nullptr;
  }
#define XE_VULKAN_LOAD_LOADER_FUNCTION(name)                 \
  functions_loaded &= (ifn.name = PFN_##name(GetProcAddress( \
                           vulkan_instance->loader_, #name))) != nullptr;
#else
#error No Vulkan loader library loading provided for the target platform.
#endif
  XE_VULKAN_LOAD_LOADER_FUNCTION(vkGetInstanceProcAddr);
  XE_VULKAN_LOAD_LOADER_FUNCTION(vkDestroyInstance);
#undef XE_VULKAN_LOAD_LOADER_FUNCTION
  if (!functions_loaded) {
    XELOGE("Failed to get Vulkan loader function pointers");
    return nullptr;
  }

  // Load global functions.

  functions_loaded &=
      (ifn.vkCreateInstance = PFN_vkCreateInstance(
           ifn.vkGetInstanceProcAddr(nullptr, "vkCreateInstance"))) != nullptr;
  functions_loaded &=
      (ifn.vkEnumerateInstanceExtensionProperties =
           PFN_vkEnumerateInstanceExtensionProperties(ifn.vkGetInstanceProcAddr(
               nullptr, "vkEnumerateInstanceExtensionProperties"))) != nullptr;
  functions_loaded &=
      (ifn.vkEnumerateInstanceLayerProperties =
           PFN_vkEnumerateInstanceLayerProperties(ifn.vkGetInstanceProcAddr(
               nullptr, "vkEnumerateInstanceLayerProperties"))) != nullptr;
  if (!functions_loaded) {
    XELOGE(
        "Failed to get Vulkan global function pointers via "
        "vkGetInstanceProcAddr");
    return nullptr;
  }
  // Available since Vulkan 1.1. If this is nullptr, it's a Vulkan 1.0 instance.
  ifn.vkEnumerateInstanceVersion = PFN_vkEnumerateInstanceVersion(
      ifn.vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"));

  // Get the API version.

  if (ifn.vkEnumerateInstanceVersion) {
    ifn.vkEnumerateInstanceVersion(&vulkan_instance->api_version_);
  }

  // Enable extensions and layers.

  // Name pointers from `requested_extensions` will be used in the enabled
  // extensions vector.
  std::unordered_map<std::string, bool*> requested_extensions;
  if (vulkan_instance->api_version_ >= VK_MAKE_API_VERSION(0, 1, 1, 0)) {
    vulkan_instance->extensions_.ext_1_1_KHR_get_physical_device_properties2 =
        true;
  } else {
    // #60.
    requested_extensions.emplace(
        "VK_KHR_get_physical_device_properties2",
        &vulkan_instance->extensions_
             .ext_1_1_KHR_get_physical_device_properties2);
  }
  // #129.
  requested_extensions.emplace(
      "VK_EXT_debug_utils", &vulkan_instance->extensions_.ext_EXT_debug_utils);
  // #395.
  requested_extensions.emplace(
      "VK_KHR_portability_enumeration",
      &vulkan_instance->extensions_.ext_KHR_portability_enumeration);
  if (with_surface) {
    // #1.
    requested_extensions.emplace("VK_KHR_surface",
                                 &vulkan_instance->extensions_.ext_KHR_surface);
#ifdef VK_USE_PLATFORM_XCB_KHR
    // #6.
    requested_extensions.emplace(
        "VK_KHR_xcb_surface",
        &vulkan_instance->extensions_.ext_KHR_xcb_surface);
#endif
#ifdef VK_USE_PLATFORM_ANDROID_KHR
    // #9.
    requested_extensions.emplace(
        "VK_KHR_android_surface",
        &vulkan_instance->extensions_.ext_KHR_android_surface);
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
    // #10.
    requested_extensions.emplace(
        "VK_KHR_win32_surface",
        &vulkan_instance->extensions_.ext_KHR_win32_surface);
#endif
  }

  std::vector<const char*> enabled_extensions;

  std::vector<VkExtensionProperties> supported_implementation_extensions;
  while (true) {
    uint32_t supported_implementation_extension_count = 0;
    const VkResult get_supported_implementation_extension_count_result =
        ifn.vkEnumerateInstanceExtensionProperties(
            nullptr, &supported_implementation_extension_count, nullptr);
    if (get_supported_implementation_extension_count_result != VK_SUCCESS &&
        get_supported_implementation_extension_count_result != VK_INCOMPLETE) {
      XELOGW("Failed to get the Vulkan instance extension count");
      return nullptr;
    }
    if (supported_implementation_extension_count) {
      supported_implementation_extensions.resize(
          supported_implementation_extension_count);
      const VkResult get_supported_implementation_extensions_result =
          ifn.vkEnumerateInstanceExtensionProperties(
              nullptr, &supported_implementation_extension_count,
              supported_implementation_extensions.data());
      if (get_supported_implementation_extensions_result == VK_INCOMPLETE) {
        continue;
      }
      if (get_supported_implementation_extensions_result != VK_SUCCESS) {
        XELOGW("Failed to get the Vulkan instance extensions");
        return nullptr;
      }
    }
    supported_implementation_extensions.resize(
        supported_implementation_extension_count);
    break;
  }

  for (const VkExtensionProperties& supported_extension :
       supported_implementation_extensions) {
    const auto requested_extension_it =
        requested_extensions.find(supported_extension.extensionName);
    if (requested_extension_it == requested_extensions.cend()) {
      continue;
    }
    assert_not_null(requested_extension_it->second);
    if (!*requested_extension_it->second) {
      enabled_extensions.emplace_back(requested_extension_it->first.c_str());
      *requested_extension_it->second = true;
    }
  }

  // If enabled layers are not present, will disable all extensions provided by
  // the layers by truncating the enabled extension vector to this size.
  const size_t enabled_implementation_extension_count =
      enabled_extensions.size();
  std::vector<bool*> enabled_layer_extension_enablement_bools;

  // Name pointers from `requested_layers` will be used in the enabled layer
  // vector.
  std::unordered_map<std::string, bool*> requested_layers;
  bool layer_khronos_validation = false;
  if (try_enable_validation) {
    requested_layers.emplace("VK_LAYER_KHRONOS_validation",
                             &layer_khronos_validation);
  }

  std::vector<const char*> enabled_layers;

  if (!requested_layers.empty()) {
    std::vector<VkLayerProperties> available_layers;
    // "The list of available layers may change at any time due to actions
    // outside of the Vulkan implementation"
    while (true) {
      available_layers.clear();
      uint32_t available_layer_count = 0;
      const VkResult get_available_layer_count_result =
          ifn.vkEnumerateInstanceLayerProperties(&available_layer_count,
                                                 nullptr);
      if (get_available_layer_count_result != VK_SUCCESS &&
          get_available_layer_count_result != VK_INCOMPLETE) {
        break;
      }
      if (available_layer_count) {
        available_layers.resize(available_layer_count);
        const VkResult get_available_layers_result =
            ifn.vkEnumerateInstanceLayerProperties(&available_layer_count,
                                                   available_layers.data());
        if (get_available_layers_result == VK_INCOMPLETE) {
          // New layers were added.
          continue;
        }
        if (get_available_layers_result != VK_SUCCESS) {
          available_layers.clear();
          break;
        }
        // In case the second enumeration returned fewer layers.
        available_layers.resize(available_layer_count);
      }
      break;
    }

    if (!available_layers.empty()) {
      std::vector<VkExtensionProperties> supported_layer_extensions;

      for (const VkLayerProperties& available_layer : available_layers) {
        auto requested_layer_it =
            requested_layers.find(available_layer.layerName);
        if (requested_layer_it == requested_layers.cend()) {
          continue;
        }

        bool got_layer_extensions = true;
        // "Because the list of available layers may change externally between
        // calls to vkEnumerateInstanceExtensionProperties, two calls may
        // retrieve different results if a pLayerName is available in one call
        // but not in another."
        while (true) {
          uint32_t supported_layer_extension_count = 0;
          const VkResult get_supported_layer_extension_count_result =
              ifn.vkEnumerateInstanceExtensionProperties(
                  nullptr, &supported_layer_extension_count, nullptr);
          if (get_supported_layer_extension_count_result != VK_SUCCESS &&
              get_supported_layer_extension_count_result != VK_INCOMPLETE) {
            got_layer_extensions = false;
            break;
          }
          if (supported_layer_extension_count) {
            supported_layer_extensions.resize(supported_layer_extension_count);
            const VkResult get_supported_layer_extensions_result =
                ifn.vkEnumerateInstanceExtensionProperties(
                    available_layer.layerName, &supported_layer_extension_count,
                    supported_layer_extensions.data());
            if (get_supported_layer_extensions_result == VK_INCOMPLETE) {
              continue;
            }
            if (get_supported_layer_extensions_result != VK_SUCCESS) {
              got_layer_extensions = false;
              break;
            }
          }
          supported_layer_extensions.resize(supported_layer_extension_count);
          break;
        }
        if (!got_layer_extensions) {
          // The layer was possibly removed.
          continue;
        }

        for (const VkExtensionProperties& supported_extension :
             supported_layer_extensions) {
          const auto requested_extension_it =
              requested_extensions.find(supported_extension.extensionName);
          if (requested_extension_it == requested_extensions.cend()) {
            continue;
          }
          assert_not_null(requested_extension_it->second);
          // Don't add the extension to the enabled vector multiple times if
          // provided by the implementation itself or by another layer.
          if (!*requested_extension_it->second) {
            enabled_extensions.emplace_back(
                requested_extension_it->first.c_str());
            enabled_layer_extension_enablement_bools.push_back(
                requested_layer_it->second);
            *requested_extension_it->second = true;
          }
        }

        assert_not_null(requested_layer_it->second);
        if (!*requested_layer_it->second) {
          enabled_layers.emplace_back(requested_layer_it->first.c_str());
          *requested_layer_it->second = true;
        }
      }
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
  // "The patch version number specified in apiVersion is ignored when creating
  // an instance object."
  // "Vulkan 1.0 implementations were required to return
  // VK_ERROR_INCOMPATIBLE_DRIVER if apiVersion was larger than 1.0."
  application_info.apiVersion =
      vulkan_instance->api_version_ >= VK_MAKE_API_VERSION(0, 1, 1, 0)
          ? VulkanDevice::kHighestUsedApiMinorVersion
          : VK_MAKE_API_VERSION(0, 1, 0, 0);

  VkInstanceCreateInfo instance_create_info;
  instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_create_info.pNext = nullptr;
  instance_create_info.flags = 0;
  // VK_KHR_get_physical_device_properties2 is needed to get the portability
  // subset features.
  if (vulkan_instance->extensions_.ext_KHR_portability_enumeration &&
      vulkan_instance->extensions_
          .ext_1_1_KHR_get_physical_device_properties2) {
    instance_create_info.flags |=
        VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }
  instance_create_info.pApplicationInfo = &application_info;
  instance_create_info.enabledLayerCount = uint32_t(enabled_layers.size());
  instance_create_info.ppEnabledLayerNames = enabled_layers.data();
  instance_create_info.enabledExtensionCount =
      uint32_t(enabled_extensions.size());
  instance_create_info.ppEnabledExtensionNames = enabled_extensions.data();
  VkResult instance_create_result = ifn.vkCreateInstance(
      &instance_create_info, nullptr, &vulkan_instance->instance_);

  if (instance_create_result == VK_ERROR_LAYER_NOT_PRESENT ||
      instance_create_result == VK_ERROR_EXTENSION_NOT_PRESENT) {
    // A layer was possibly removed. Try without layers.
    for (bool* const extension_enablement :
         enabled_layer_extension_enablement_bools) {
      *extension_enablement = false;
    }
    for (const std::pair<std::string, bool*>& requested_layer :
         requested_layers) {
      *requested_layer.second = false;
    }
    instance_create_info.enabledLayerCount = 0;
    instance_create_info.enabledExtensionCount =
        uint32_t(enabled_implementation_extension_count);
    instance_create_result = ifn.vkCreateInstance(
        &instance_create_info, nullptr, &vulkan_instance->instance_);
  }

  if (instance_create_result != VK_SUCCESS) {
    XELOGE("Failed to create a Vulkan instance: {}",
           vk::to_string(vk::Result(instance_create_result)));
    return nullptr;
  }

  // Load instance functions.

#define XE_UI_VULKAN_FUNCTION(name)                                     \
  functions_loaded &= (ifn.name = PFN_##name(ifn.vkGetInstanceProcAddr( \
                           vulkan_instance->instance_, #name))) != nullptr;

  // Vulkan 1.0.
#include "xenia/ui/vulkan/functions/instance_1_0.inc"

  // Extensions promoted to a Vulkan version supported by the instance.
#define XE_UI_VULKAN_FUNCTION_PROMOTED(extension_name, core_name) \
  functions_loaded &=                                             \
      (ifn.core_name = PFN_##core_name(ifn.vkGetInstanceProcAddr( \
           vulkan_instance->instance_, #core_name))) != nullptr;
  if (vulkan_instance->api_version_ >= VK_MAKE_API_VERSION(0, 1, 1, 0)) {
#include "xenia/ui/vulkan/functions/instance_1_1_khr_get_physical_device_properties2.inc"
  }
#undef XE_UI_VULKAN_FUNCTION_PROMOTED

  // Non-promoted extensions, and extensions promoted to a Vulkan version not
  // supported by the instance.
#define XE_UI_VULKAN_FUNCTION_PROMOTED(extension_name, core_name) \
  functions_loaded &=                                             \
      (ifn.core_name = PFN_##core_name(ifn.vkGetInstanceProcAddr( \
           vulkan_instance->instance_, #extension_name))) != nullptr;
  if (vulkan_instance->api_version_ < VK_MAKE_API_VERSION(0, 1, 1, 0)) {
    if (vulkan_instance->extensions_
            .ext_1_1_KHR_get_physical_device_properties2) {
#include "xenia/ui/vulkan/functions/instance_1_1_khr_get_physical_device_properties2.inc"
    }
  }
#ifdef VK_USE_PLATFORM_XCB_KHR
  if (vulkan_instance->extensions_.ext_KHR_xcb_surface) {
#include "xenia/ui/vulkan/functions/instance_khr_xcb_surface.inc"
  }
#endif
#ifdef VK_USE_PLATFORM_ANDROID_KHR
  if (vulkan_instance->extensions_.ext_KHR_android_surface) {
#include "xenia/ui/vulkan/functions/instance_khr_android_surface.inc"
  }
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
  if (vulkan_instance->extensions_.ext_KHR_win32_surface) {
#include "xenia/ui/vulkan/functions/instance_khr_win32_surface.inc"
  }
#endif
  if (vulkan_instance->extensions_.ext_KHR_surface) {
#include "xenia/ui/vulkan/functions/instance_khr_surface.inc"
  }
  if (vulkan_instance->extensions_.ext_EXT_debug_utils) {
#include "xenia/ui/vulkan/functions/instance_ext_debug_utils.inc"
  }
#undef XE_UI_VULKAN_FUNCTION_PROMOTED

#undef XE_UI_VULKAN_FUNCTION

  if (!functions_loaded) {
    XELOGE("Failed to get all Vulkan instance function pointers");
    return nullptr;
  }

  // Check whether a surface can be created.

  if (with_surface && !VulkanPresenter::GetSurfaceTypesSupportedByInstance(
                          vulkan_instance->extensions_)) {
    XELOGE("The Vulkan instance doesn't support surface types used by Xenia");
    return nullptr;
  }

  // Log instance properties.

  XELOGI("Vulkan instance API version {}.{}.{}. Enabled layers and extensions:",
         VK_VERSION_MAJOR(vulkan_instance->api_version_),
         VK_VERSION_MINOR(vulkan_instance->api_version_),
         VK_VERSION_PATCH(vulkan_instance->api_version_));
  for (uint32_t enabled_layer_index = 0;
       enabled_layer_index < instance_create_info.enabledLayerCount;
       ++enabled_layer_index) {
    XELOGI("* {}",
           instance_create_info.ppEnabledLayerNames[enabled_layer_index]);
  }
  for (uint32_t enabled_extension_index = 0;
       enabled_extension_index < instance_create_info.enabledExtensionCount;
       ++enabled_extension_index) {
    XELOGI(
        "* {}",
        instance_create_info.ppEnabledExtensionNames[enabled_extension_index]);
  }

  // Create the debug messenger if requested and available.

  if (vulkan_instance->extensions_.ext_EXT_debug_utils &&
      cvars::vulkan_log_debug_messages) {
    VkDebugUtilsMessengerCreateInfoEXT debug_utils_messenger_create_info = {
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    if (xe::logging::ShouldLog(xe::LogLevel::Debug)) {
      debug_utils_messenger_create_info.messageSeverity |=
          VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    }
    if (xe::logging::ShouldLog(xe::LogLevel::Info)) {
      debug_utils_messenger_create_info.messageSeverity |=
          VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
    }
    if (xe::logging::ShouldLog(xe::LogLevel::Warning)) {
      debug_utils_messenger_create_info.messageSeverity |=
          VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
    }
    if (xe::logging::ShouldLog(xe::LogLevel::Error)) {
      debug_utils_messenger_create_info.messageSeverity |=
          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    }
    // VUID-VkDebugUtilsMessengerCreateInfoEXT-messageSeverity-requiredbitmask:
    // "messageSeverity must not be 0"
    if (debug_utils_messenger_create_info.messageSeverity) {
      debug_utils_messenger_create_info.messageType =
          VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
          VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      debug_utils_messenger_create_info.pfnUserCallback =
          DebugUtilsMessengerCallback;
      debug_utils_messenger_create_info.pUserData = vulkan_instance.get();
      const VkResult debug_utils_messenger_create_result =
          ifn.vkCreateDebugUtilsMessengerEXT(
              vulkan_instance->instance_, &debug_utils_messenger_create_info,
              nullptr, &vulkan_instance->debug_utils_messenger_);
      if (debug_utils_messenger_create_result != VK_SUCCESS) {
        XELOGW("Failed to create the Vulkan debug utils messenger: {}",
               vk::to_string(vk::Result(debug_utils_messenger_create_result)));
      }
    }
  }

  return vulkan_instance;
}

VulkanInstance::~VulkanInstance() {
  if (instance_) {
    if (debug_utils_messenger_ != VK_NULL_HANDLE) {
      functions_.vkDestroyDebugUtilsMessengerEXT(
          instance_, debug_utils_messenger_, nullptr);
    }

    functions_.vkDestroyInstance(instance_, nullptr);
  }

#if XE_PLATFORM_LINUX
  if (loader_) {
    dlclose(loader_);
  }
#elif XE_PLATFORM_WIN32
  if (loader_) {
    FreeLibrary(loader_);
  }
#endif
}

void VulkanInstance::EnumeratePhysicalDevices(
    std::vector<VkPhysicalDevice>& physical_devices_out) const {
  physical_devices_out.clear();
  while (true) {
    uint32_t physical_device_count = 0;
    const VkResult get_physical_device_count_result =
        functions_.vkEnumeratePhysicalDevices(instance_, &physical_device_count,
                                              nullptr);
    if ((get_physical_device_count_result != VK_SUCCESS &&
         get_physical_device_count_result != VK_INCOMPLETE) ||
        !physical_device_count) {
      return;
    }
    physical_devices_out.resize(physical_device_count);
    const VkResult get_physical_devices_result =
        functions_.vkEnumeratePhysicalDevices(instance_, &physical_device_count,
                                              physical_devices_out.data());
    if (get_physical_devices_result == VK_INCOMPLETE) {
      continue;
    }
    physical_devices_out.resize(
        get_physical_devices_result == VK_SUCCESS ? physical_device_count : 0);
    return;
  }
}

VkBool32 VulkanInstance::DebugUtilsMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_types,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    [[maybe_unused]] void* user_data) {
  xe::LogLevel log_level;
  char log_prefix_char;
  if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    log_level = xe::LogLevel::Error;
    log_prefix_char = xe::logging::kPrefixCharError;
  } else if (message_severity >=
             VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    log_level = xe::LogLevel::Warning;
    log_prefix_char = xe::logging::kPrefixCharWarning;
  } else if (message_severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    log_level = xe::LogLevel::Info;
    log_prefix_char = xe::logging::kPrefixCharInfo;
  } else {
    log_level = xe::LogLevel::Debug;
    log_prefix_char = xe::logging::kPrefixCharDebug;
  }

  std::ostringstream log_str;

  log_str << "Vulkan "
          << vk::to_string(
                 vk::DebugUtilsMessageSeverityFlagBitsEXT(message_severity))
          << " ("
          << vk::to_string(vk::DebugUtilsMessageTypeFlagsEXT(message_types))
          << ", ID " << callback_data->messageIdNumber;
  if (callback_data->pMessageIdName) {
    log_str << ": " << callback_data->pMessageIdName;
  }
  log_str << ')';

  if (callback_data->pMessage) {
    log_str << ": " << callback_data->pMessage;
  }

  bool annotations_begun = false;
  const auto begin_annotation = [&log_str, &annotations_begun]() {
    log_str << (annotations_begun ? ", " : " (");
    annotations_begun = true;
  };

  for (uint32_t queue_label_index = 0;
       queue_label_index < callback_data->queueLabelCount;
       ++queue_label_index) {
    begin_annotation();
    log_str << "queue label " << queue_label_index << ": "
            << callback_data->pQueueLabels[queue_label_index].pLabelName;
  }

  for (uint32_t cmd_buf_label_index = 0;
       cmd_buf_label_index < callback_data->cmdBufLabelCount;
       ++cmd_buf_label_index) {
    begin_annotation();
    log_str << "command buffer label " << cmd_buf_label_index << ": "
            << callback_data->pCmdBufLabels[cmd_buf_label_index].pLabelName;
  }

  for (uint32_t object_index = 0; object_index < callback_data->objectCount;
       ++object_index) {
    begin_annotation();
    const VkDebugUtilsObjectNameInfoEXT& object_info =
        callback_data->pObjects[object_index];
    // Lowercase hexadecimal digits in the handle to match the default Vulkan
    // debug utils messenger.
    log_str << "object " << object_index << ": "
            << vk::to_string(vk::ObjectType(object_info.objectType)) << " 0x"
            << std::hex << object_info.objectHandle << std::dec;
    if (object_info.pObjectName) {
      log_str << " '" << object_info.pObjectName << '\'';
    }
  }

  if (annotations_begun) {
    log_str << ')';
  }

  xe::logging::AppendLogLine(log_level, log_prefix_char, log_str.str());

  return VK_FALSE;
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
