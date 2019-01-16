/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan_instance.h"

#include <cinttypes>
#include <cstring>
#include <mutex>
#include <string>

#include "third_party/renderdoc/renderdoc_app.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/platform.h"
#include "xenia/base/profiling.h"
#include "xenia/ui/vulkan/vulkan.h"
#include "xenia/ui/vulkan/vulkan_immediate_drawer.h"
#include "xenia/ui/vulkan/vulkan_util.h"
#include "xenia/ui/window.h"

#if XE_PLATFORM_LINUX
#include <dlfcn.h>
#elif XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

#if XE_PLATFORM_LINUX
#include "xenia/ui/window_gtk.h"
#endif

#define VK_API_VERSION VK_API_VERSION_1_1

namespace xe {
namespace ui {
namespace vulkan {

VulkanInstance::VulkanInstance() {
  if (cvars::vulkan_validation) {
    DeclareRequiredLayer("VK_LAYER_LUNARG_standard_validation",
                         Version::Make(0, 0, 0), true);
    // DeclareRequiredLayer("VK_LAYER_GOOGLE_unique_objects", Version::Make(0,
    // 0, 0), true);
    /*
    DeclareRequiredLayer("VK_LAYER_GOOGLE_threading", Version::Make(0, 0, 0),
                         true);
    DeclareRequiredLayer("VK_LAYER_LUNARG_core_validation",
                         Version::Make(0, 0, 0), true);
    DeclareRequiredLayer("VK_LAYER_LUNARG_object_tracker",
                         Version::Make(0, 0, 0), true);
    DeclareRequiredLayer("VK_LAYER_LUNARG_draw_state", Version::Make(0, 0, 0),
                         true);
    DeclareRequiredLayer("VK_LAYER_LUNARG_parameter_validation",
                         Version::Make(0, 0, 0), true);
    DeclareRequiredLayer("VK_LAYER_LUNARG_swapchain", Version::Make(0, 0, 0),
                         true);
    DeclareRequiredLayer("VK_LAYER_LUNARG_device_limits",
                         Version::Make(0, 0, 0), true);
    DeclareRequiredLayer("VK_LAYER_LUNARG_image", Version::Make(0, 0, 0), true);
    */
    DeclareRequiredExtension(VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
                             Version::Make(0, 0, 0), true);
  }

  DeclareRequiredExtension(VK_EXT_DEBUG_MARKER_EXTENSION_NAME,
                           Version::Make(0, 0, 0), true);
  DeclareRequiredExtension(VK_KHR_SURFACE_EXTENSION_NAME,
                           Version::Make(0, 0, 0), true);
#if XE_PLATFORM_WIN32
  DeclareRequiredExtension(VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
                           Version::Make(0, 0, 0), true);
#elif XE_PLATFORM_LINUX
#ifdef GDK_WINDOWING_X11
  DeclareRequiredExtension(VK_KHR_XCB_SURFACE_EXTENSION_NAME,
                           Version::Make(0, 0, 0), true);
#else
#error No Vulkan surface extension for the GDK backend defined yet.
#endif
#else
#error No Vulkan surface extension for the platform defined yet.
#endif
}

VulkanInstance::~VulkanInstance() { DestroyInstance(); }

bool VulkanInstance::Initialize() {
  auto version = Version::Parse(VK_API_VERSION);
  XELOGVK("Initializing Vulkan {}...", version.pretty_string);

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

  // Get all of the global layers and extensions provided by the system.
  if (!QueryGlobals()) {
    XELOGE("Failed to query instance globals");
    return false;
  }

  // Create the vulkan instance used by the application with our required
  // extensions and layers.
  if (!CreateInstance()) {
    XELOGE("Failed to create instance");
    return false;
  }

  // Query available devices so that we can pick one.
  if (!QueryDevices()) {
    XELOGE("Failed to query devices");
    return false;
  }

  // Hook into renderdoc, if it's available.
  EnableRenderDoc();

  XELOGVK("Instance initialized successfully!");
  return true;
}

bool VulkanInstance::EnableRenderDoc() {
  // RenderDoc injects itself into our process, so we should be able to get it.
  pRENDERDOC_GetAPI get_api = nullptr;
#if XE_PLATFORM_WIN32
  auto module_handle = GetModuleHandleW(L"renderdoc.dll");
  if (!module_handle) {
    XELOGI("RenderDoc support requested but it is not attached");
    return false;
  }
  get_api = reinterpret_cast<pRENDERDOC_GetAPI>(
      GetProcAddress(module_handle, "RENDERDOC_GetAPI"));
#else
// TODO(benvanik): dlsym/etc - abstracted in base/.
#endif  // XE_PLATFORM_32
  if (!get_api) {
    XELOGI("RenderDoc support requested but it is not attached");
    return false;
  }

  // Request all API function pointers.
  if (!get_api(eRENDERDOC_API_Version_1_0_1,
               reinterpret_cast<void**>(&renderdoc_api_))) {
    XELOGE("RenderDoc found but was unable to get API - version mismatch?");
    return false;
  }
  auto api = reinterpret_cast<RENDERDOC_API_1_0_1*>(renderdoc_api_);

  // Query version.
  int major;
  int minor;
  int patch;
  api->GetAPIVersion(&major, &minor, &patch);
  XELOGI("RenderDoc attached; {}.{}.{}", major, minor, patch);

  is_renderdoc_attached_ = true;

  return true;
}

bool VulkanInstance::QueryGlobals() {
  // Scan global layers and accumulate properties.
  // We do this in a loop so that we can allocate the required amount of
  // memory and handle race conditions while querying.
  uint32_t count = 0;
  std::vector<VkLayerProperties> global_layer_properties;
  VkResult err;
  do {
    err = lfn_.vkEnumerateInstanceLayerProperties(&count, nullptr);
    CheckResult(err, "vkEnumerateInstanceLayerProperties");
    global_layer_properties.resize(count);
    err = lfn_.vkEnumerateInstanceLayerProperties(
        &count, global_layer_properties.data());
  } while (err == VK_INCOMPLETE);
  CheckResult(err, "vkEnumerateInstanceLayerProperties");
  global_layers_.resize(count);
  for (size_t i = 0; i < global_layers_.size(); ++i) {
    auto& global_layer = global_layers_[i];
    global_layer.properties = global_layer_properties[i];

    // Get all extensions available for the layer.
    do {
      err = lfn_.vkEnumerateInstanceExtensionProperties(
          global_layer.properties.layerName, &count, nullptr);
      CheckResult(err, "vkEnumerateInstanceExtensionProperties");
      global_layer.extensions.resize(count);
      err = lfn_.vkEnumerateInstanceExtensionProperties(
          global_layer.properties.layerName, &count,
          global_layer.extensions.data());
    } while (err == VK_INCOMPLETE);
    CheckResult(err, "vkEnumerateInstanceExtensionProperties");
  }
  XELOGVK("Found {} global layers:", global_layers_.size());
  for (size_t i = 0; i < global_layers_.size(); ++i) {
    auto& global_layer = global_layers_[i];
    auto spec_version = Version::Parse(global_layer.properties.specVersion);
    auto impl_version =
        Version::Parse(global_layer.properties.implementationVersion);
    XELOGVK("- {} (spec: {}, impl: {})", global_layer.properties.layerName,
            spec_version.pretty_string, impl_version.pretty_string);
    XELOGVK("  {}", global_layer.properties.description);
    if (!global_layer.extensions.empty()) {
      XELOGVK("  {} extensions:", global_layer.extensions.size());
      DumpExtensions(global_layer.extensions, "  ");
    }
  }

  // Scan global extensions.
  do {
    err = lfn_.vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    CheckResult(err, "vkEnumerateInstanceExtensionProperties");
    global_extensions_.resize(count);
    err = lfn_.vkEnumerateInstanceExtensionProperties(
        nullptr, &count, global_extensions_.data());
  } while (err == VK_INCOMPLETE);
  CheckResult(err, "vkEnumerateInstanceExtensionProperties");
  XELOGVK("Found {} global extensions:", global_extensions_.size());
  DumpExtensions(global_extensions_, "");

  return true;
}

bool VulkanInstance::CreateInstance() {
  XELOGVK("Verifying layers and extensions...");

  // Gather list of enabled layer names.
  auto layers_result = CheckRequirements(required_layers_, global_layers_);
  auto& enabled_layers = layers_result.second;

  // Gather list of enabled extension names.
  auto extensions_result =
      CheckRequirements(required_extensions_, global_extensions_);
  auto& enabled_extensions = extensions_result.second;

  // We wait until both extensions and layers are checked before failing out so
  // that the user gets a complete list of what they have/don't.
  if (!extensions_result.first || !layers_result.first) {
    XELOGE("Layer and extension verification failed; aborting initialization");
    return false;
  }

  XELOGVK("Initializing application instance...");

  // TODO(benvanik): use GetEntryInfo?
  VkApplicationInfo application_info;
  application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application_info.pNext = nullptr;
  application_info.pApplicationName = "xenia";
  application_info.applicationVersion = 1;
  application_info.pEngineName = "xenia";
  application_info.engineVersion = 1;
  application_info.apiVersion = VK_API_VERSION;

  VkInstanceCreateInfo instance_info;
  instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_info.pNext = nullptr;
  instance_info.flags = 0;
  instance_info.pApplicationInfo = &application_info;
  instance_info.enabledLayerCount =
      static_cast<uint32_t>(enabled_layers.size());
  instance_info.ppEnabledLayerNames = enabled_layers.data();
  instance_info.enabledExtensionCount =
      static_cast<uint32_t>(enabled_extensions.size());
  instance_info.ppEnabledExtensionNames = enabled_extensions.data();

  auto err = lfn_.vkCreateInstance(&instance_info, nullptr, &handle);
  if (err != VK_SUCCESS) {
    XELOGE("vkCreateInstance returned {}", to_string(err));
  }
  switch (err) {
    case VK_SUCCESS:
      // Ok!
      break;
    case VK_ERROR_INITIALIZATION_FAILED:
      XELOGE("Instance initialization failed; generic");
      return false;
    case VK_ERROR_INCOMPATIBLE_DRIVER:
      XELOGE(
          "Instance initialization failed; cannot find a compatible Vulkan "
          "installable client driver (ICD)");
      return false;
    case VK_ERROR_EXTENSION_NOT_PRESENT:
      XELOGE("Instance initialization failed; requested extension not present");
      return false;
    case VK_ERROR_LAYER_NOT_PRESENT:
      XELOGE("Instance initialization failed; requested layer not present");
      return false;
    default:
      XELOGE("Instance initialization failed; unknown: {}", to_string(err));
      return false;
  }

  // Check if extensions are enabled.
  dbg_report_ena_ = false;
  for (const char* enabled_extension : enabled_extensions) {
    if (!dbg_report_ena_ &&
        !std::strcmp(enabled_extension, VK_EXT_DEBUG_REPORT_EXTENSION_NAME)) {
      dbg_report_ena_ = true;
    }
  }

  // Get instance functions.
  std::memset(&ifn_, 0, sizeof(ifn_));
  bool instance_functions_loaded = true;
#define XE_UI_VULKAN_FUNCTION(name)                                          \
  instance_functions_loaded &=                                               \
      (ifn_.name = PFN_##name(lfn_.vkGetInstanceProcAddr(handle, #name))) != \
      nullptr;
#include "xenia/ui/vulkan/functions/instance_1_0.inc"
#include "xenia/ui/vulkan/functions/instance_khr_surface.inc"
#if XE_PLATFORM_ANDROID
#include "xenia/ui/vulkan/functions/instance_khr_android_surface.inc"
#elif XE_PLATFORM_GNU_LINUX
#include "xenia/ui/vulkan/functions/instance_khr_xcb_surface.inc"
#elif XE_PLATFORM_WIN32
#include "xenia/ui/vulkan/functions/instance_khr_win32_surface.inc"
#endif
  if (dbg_report_ena_) {
#include "xenia/ui/vulkan/functions/instance_ext_debug_report.inc"
  }
#undef XE_VULKAN_LOAD_IFN
  if (!instance_functions_loaded) {
    XELOGE("Failed to get Vulkan instance function pointers");
    return false;
  }

  // Enable debug validation, if needed.
  EnableDebugValidation();

  return true;
}

void VulkanInstance::DestroyInstance() {
  if (handle) {
    DisableDebugValidation();
    lfn_.vkDestroyInstance(handle, nullptr);
    handle = nullptr;
  }

#if XE_PLATFORM_LINUX
  if (library_) {
    dlclose(library_);
    library_ = nullptr;
  }
#elif XE_PLATFORM_WIN32
  if (library_) {
    FreeLibrary(library_);
    library_ = nullptr;
  }
#endif
}

VkBool32 VKAPI_PTR DebugMessageCallback(VkDebugReportFlagsEXT flags,
                                        VkDebugReportObjectTypeEXT objectType,
                                        uint64_t object, size_t location,
                                        int32_t messageCode,
                                        const char* pLayerPrefix,
                                        const char* pMessage, void* pUserData) {
  if (strcmp(pLayerPrefix, "Validation") == 0) {
    const char* blacklist[] = {
        "bound but it was never updated. You may want to either update it or "
        "not bind it.",
        "is being used in draw but has not been updated.",
    };
    for (uint32_t i = 0; i < xe::countof(blacklist); ++i) {
      if (strstr(pMessage, blacklist[i]) != nullptr) {
        return false;
      }
    }
  }

  auto instance = reinterpret_cast<VulkanInstance*>(pUserData);
  const char* message_type = "UNKNOWN";
  if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
    message_type = "ERROR";
  } else if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
    message_type = "WARN";
  } else if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
    message_type = "PERF WARN";
  } else if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
    message_type = "INFO";
  } else if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
    message_type = "DEBUG";
  }

  XELOGVK("[{}/{}:{}] {}", pLayerPrefix, message_type, messageCode, pMessage);
  return false;
}

void VulkanInstance::EnableDebugValidation() {
  if (!dbg_report_ena_) {
    XELOGVK("Debug validation layer not installed; ignoring");
    return;
  }
  if (dbg_report_callback_) {
    DisableDebugValidation();
  }
  VkDebugReportCallbackCreateInfoEXT create_info;
  create_info.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT;
  create_info.pNext = nullptr;
  // TODO(benvanik): flags to set these.
  create_info.flags =
      VK_DEBUG_REPORT_INFORMATION_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
      VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
      VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
  create_info.pfnCallback = &DebugMessageCallback;
  create_info.pUserData = this;
  auto status = ifn_.vkCreateDebugReportCallbackEXT(
      handle, &create_info, nullptr, &dbg_report_callback_);
  if (status == VK_SUCCESS) {
    XELOGVK("Debug validation layer enabled");
  } else {
    XELOGVK("Debug validation layer failed to install; error {}",
            to_string(status));
  }
}

void VulkanInstance::DisableDebugValidation() {
  if (!dbg_report_ena_ || !dbg_report_callback_) {
    return;
  }
  ifn_.vkDestroyDebugReportCallbackEXT(handle, dbg_report_callback_, nullptr);
  dbg_report_callback_ = nullptr;
}

bool VulkanInstance::QueryDevices() {
  // Get handles to all devices.
  uint32_t count = 0;
  std::vector<VkPhysicalDevice> device_handles;
  auto err = ifn_.vkEnumeratePhysicalDevices(handle, &count, nullptr);
  CheckResult(err, "vkEnumeratePhysicalDevices");

  device_handles.resize(count);
  err = ifn_.vkEnumeratePhysicalDevices(handle, &count, device_handles.data());
  CheckResult(err, "vkEnumeratePhysicalDevices");

  // Query device info.
  for (size_t i = 0; i < device_handles.size(); ++i) {
    auto device_handle = device_handles[i];
    DeviceInfo device_info;
    device_info.handle = device_handle;

    // Query general attributes.
    ifn_.vkGetPhysicalDeviceProperties(device_handle, &device_info.properties);
    ifn_.vkGetPhysicalDeviceFeatures(device_handle, &device_info.features);
    ifn_.vkGetPhysicalDeviceMemoryProperties(device_handle,
                                             &device_info.memory_properties);

    // Gather queue family properties.
    ifn_.vkGetPhysicalDeviceQueueFamilyProperties(device_handle, &count,
                                                  nullptr);
    device_info.queue_family_properties.resize(count);
    ifn_.vkGetPhysicalDeviceQueueFamilyProperties(
        device_handle, &count, device_info.queue_family_properties.data());

    // Gather layers.
    std::vector<VkLayerProperties> layer_properties;
    err = ifn_.vkEnumerateDeviceLayerProperties(device_handle, &count, nullptr);
    CheckResult(err, "vkEnumerateDeviceLayerProperties");
    layer_properties.resize(count);
    err = ifn_.vkEnumerateDeviceLayerProperties(device_handle, &count,
                                                layer_properties.data());
    CheckResult(err, "vkEnumerateDeviceLayerProperties");
    for (size_t j = 0; j < layer_properties.size(); ++j) {
      LayerInfo layer_info;
      layer_info.properties = layer_properties[j];
      err = ifn_.vkEnumerateDeviceExtensionProperties(
          device_handle, layer_info.properties.layerName, &count, nullptr);
      CheckResult(err, "vkEnumerateDeviceExtensionProperties");
      layer_info.extensions.resize(count);
      err = ifn_.vkEnumerateDeviceExtensionProperties(
          device_handle, layer_info.properties.layerName, &count,
          layer_info.extensions.data());
      CheckResult(err, "vkEnumerateDeviceExtensionProperties");
      device_info.layers.push_back(std::move(layer_info));
    }

    // Gather extensions.
    err = ifn_.vkEnumerateDeviceExtensionProperties(device_handle, nullptr,
                                                    &count, nullptr);
    CheckResult(err, "vkEnumerateDeviceExtensionProperties");
    device_info.extensions.resize(count);
    err = ifn_.vkEnumerateDeviceExtensionProperties(
        device_handle, nullptr, &count, device_info.extensions.data());
    CheckResult(err, "vkEnumerateDeviceExtensionProperties");

    available_devices_.push_back(std::move(device_info));
  }

  XELOGVK("Found {} physical devices:", available_devices_.size());
  for (size_t i = 0; i < available_devices_.size(); ++i) {
    auto& device_info = available_devices_[i];
    XELOGVK("- Device {}:", i);
    DumpDeviceInfo(device_info);
  }

  return true;
}

void VulkanInstance::DumpLayers(const std::vector<LayerInfo>& layers,
                                const char* indent) {
  for (size_t i = 0; i < layers.size(); ++i) {
    auto& layer = layers[i];
    auto spec_version = Version::Parse(layer.properties.specVersion);
    auto impl_version = Version::Parse(layer.properties.implementationVersion);
    XELOGVK("{}- {} (spec: {}, impl: {})", indent, layer.properties.layerName,
            spec_version.pretty_string, impl_version.pretty_string);
    XELOGVK("{}  {}", indent, layer.properties.description);
    if (!layer.extensions.empty()) {
      XELOGVK("{}  {} extensions:", indent, layer.extensions.size());
      DumpExtensions(layer.extensions, std::strlen(indent) ? "    " : "  ");
    }
  }
}

void VulkanInstance::DumpExtensions(
    const std::vector<VkExtensionProperties>& extensions, const char* indent) {
  for (size_t i = 0; i < extensions.size(); ++i) {
    auto& extension = extensions[i];
    auto version = Version::Parse(extension.specVersion);
    XELOGVK("{}- {} ({})", indent, extension.extensionName,
            version.pretty_string);
  }
}

void VulkanInstance::DumpDeviceInfo(const DeviceInfo& device_info) {
  auto& properties = device_info.properties;
  auto api_version = Version::Parse(properties.apiVersion);
  auto driver_version = Version::Parse(properties.driverVersion);
  XELOGVK("  apiVersion     = {}", api_version.pretty_string);
  XELOGVK("  driverVersion  = {}", driver_version.pretty_string);
  XELOGVK("  vendorId       = {:#04x}", properties.vendorID);
  XELOGVK("  deviceId       = {:#04x}", properties.deviceID);
  XELOGVK("  deviceType     = {}", to_string(properties.deviceType));
  XELOGVK("  deviceName     = {}", properties.deviceName);

  auto& memory_props = device_info.memory_properties;
  XELOGVK("  Memory Heaps:");
  for (size_t j = 0; j < memory_props.memoryHeapCount; ++j) {
    XELOGVK("  - Heap {}: {} bytes", j, memory_props.memoryHeaps[j].size);
    for (size_t k = 0; k < memory_props.memoryTypeCount; ++k) {
      if (memory_props.memoryTypes[k].heapIndex == j) {
        XELOGVK("    - Type {}:", k);
        auto type_flags = memory_props.memoryTypes[k].propertyFlags;
        if (!type_flags) {
          XELOGVK("      VK_MEMORY_PROPERTY_DEVICE_ONLY");
        }
        if (type_flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
          XELOGVK("      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT");
        }
        if (type_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
          XELOGVK("      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT");
        }
        if (type_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
          XELOGVK("      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT");
        }
        if (type_flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
          XELOGVK("      VK_MEMORY_PROPERTY_HOST_CACHED_BIT");
        }
        if (type_flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) {
          XELOGVK("      VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT");
        }
      }
    }
  }

  XELOGVK("  Queue Families:");
  for (size_t j = 0; j < device_info.queue_family_properties.size(); ++j) {
    auto& queue_props = device_info.queue_family_properties[j];
    XELOGVK("  - Queue {}:", j);
    XELOGVK(
        "    queueFlags         = {}{}{}{}",
        (queue_props.queueFlags & VK_QUEUE_GRAPHICS_BIT) ? "graphics, " : "",
        (queue_props.queueFlags & VK_QUEUE_COMPUTE_BIT) ? "compute, " : "",
        (queue_props.queueFlags & VK_QUEUE_TRANSFER_BIT) ? "transfer, " : "",
        (queue_props.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) ? "sparse, "
                                                               : "");
    XELOGVK("    queueCount         = {}", queue_props.queueCount);
    XELOGVK("    timestampValidBits = {}", queue_props.timestampValidBits);
  }

  XELOGVK("  Layers:");
  DumpLayers(device_info.layers, "  ");

  XELOGVK("  Extensions:");
  DumpExtensions(device_info.extensions, "  ");
}

}  // namespace vulkan
}  // namespace ui
}  // namespace xe
