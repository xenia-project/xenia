/*
 * Copyright (c) 2015-2016 The Khronos Group Inc.
 * Copyright (c) 2015-2016 Valve Corporation
 * Copyright (c) 2015-2016 LunarG, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Author: Mark Lobodzinski <mark@lunarg.com>
 *
 */

#include "vk_loader_platform.h"
#include "loader.h"

bool extension_instance_gpa(struct loader_instance *ptr_instance,
                            const char *name, void **addr);

void extensions_create_instance(struct loader_instance *ptr_instance,
                                const VkInstanceCreateInfo *pCreateInfo);

// Instance extension terminators for the VK_KHR_get_physical_device_properties2
// extension

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceFeatures2KHR(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2KHR *pFeatures);

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceProperties2KHR(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties2KHR *pProperties);

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceFormatProperties2KHR(
    VkPhysicalDevice physicalDevice, VkFormat format,
    VkFormatProperties2KHR *pFormatProperties);

VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetPhysicalDeviceImageFormatProperties2KHR(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2KHR *pImageFormatInfo,
    VkImageFormatProperties2KHR *pImageFormatProperties);

VKAPI_ATTR void VKAPI_CALL
terminator_GetPhysicalDeviceQueueFamilyProperties2KHR(
    VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2KHR *pQueueFamilyProperties);

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceMemoryProperties2KHR(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties2KHR *pMemoryProperties);

VKAPI_ATTR void VKAPI_CALL
terminator_GetPhysicalDeviceSparseImageFormatProperties2KHR(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceSparseImageFormatInfo2KHR *pFormatInfo,
    uint32_t *pPropertyCount, VkSparseImageFormatProperties2KHR *pProperties);

// Instance extension terminators for the VK_EXT_acquire_xlib_display
// extension

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
VKAPI_ATTR VkResult VKAPI_CALL terminator_AcquireXlibDisplayEXT(
    VkPhysicalDevice physicalDevice, Display *dpy, VkDisplayKHR display);

VKAPI_ATTR VkResult VKAPI_CALL terminator_GetRandROutputDisplayEXT(
    VkPhysicalDevice physicalDevice, Display *dpy, RROutput rrOutput,
    VkDisplayKHR *pDisplay);
#endif /* VK_USE_PLATFORM_XLIB_XRANDR_EXT */

// Instance extension terminators for the VK_EXT_direct_mode_display
// extension

VKAPI_ATTR VkResult VKAPI_CALL terminator_ReleaseDisplayEXT(
    VkPhysicalDevice physicalDevice, VkDisplayKHR display);

// Instance extension terminators for the VK_EXT_display_surface_counter
// extension

VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetPhysicalDeviceSurfaceCapabilities2EXT(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilities2EXT *pSurfaceCapabilities);

// Device extension terminators for the VK_NV_external_memory_capabilities
// extension

VKAPI_ATTR VkResult VKAPI_CALL terminator_DebugMarkerSetObjectTagEXT(
    VkDevice device, VkDebugMarkerObjectTagInfoEXT *pTagInfo);

VKAPI_ATTR VkResult VKAPI_CALL terminator_DebugMarkerSetObjectNameEXT(
    VkDevice device, VkDebugMarkerObjectNameInfoEXT *pNameInfo);

// Instance extension terminators for the VK_NV_external_memory_capabilities
// extension

VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetPhysicalDeviceExternalImageFormatPropertiesNV(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
    VkExternalMemoryHandleTypeFlagsNV externalHandleType,
    VkExternalImageFormatPropertiesNV *pExternalImageFormatProperties);

// Instance extension terminators for the VK_NVX_device_generated_commands
// extension
VKAPI_ATTR void VKAPI_CALL
terminator_GetPhysicalDeviceGeneratedCommandsPropertiesNVX(
    VkPhysicalDevice physicalDevice,
    VkDeviceGeneratedCommandsFeaturesNVX *pFeatures,
    VkDeviceGeneratedCommandsLimitsNVX *pLimits);
