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
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vk_loader_platform.h"
#include "loader.h"
#include "extensions.h"
#include "table_ops.h"
#include <vulkan/vk_icd.h>
#include "wsi.h"

// Definitions for the VK_KHR_get_physical_device_properties2 extension

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFeatures2KHR(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2KHR *pFeatures) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceFeatures2KHR(unwrapped_phys_dev, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceFeatures2KHR(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2KHR *pFeatures) {
    struct loader_physical_device_term *phys_dev_term =
        (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL == icd_term->GetPhysicalDeviceFeatures2KHR) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "ICD associated with VkPhysicalDevice does not support "
                   "vkGetPhysicalDeviceFeatures2KHR");
    }
    icd_term->GetPhysicalDeviceFeatures2KHR(phys_dev_term->phys_dev, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice,
                                  VkPhysicalDeviceProperties2KHR *pProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceProperties2KHR(unwrapped_phys_dev, pProperties);
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceProperties2KHR(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceProperties2KHR *pProperties) {
    struct loader_physical_device_term *phys_dev_term =
        (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL == icd_term->GetPhysicalDeviceProperties2KHR) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "ICD associated with VkPhysicalDevice does not support "
                   "vkGetPhysicalDeviceProperties2KHR");
    }
    icd_term->GetPhysicalDeviceProperties2KHR(phys_dev_term->phys_dev,
                                              pProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceFormatProperties2KHR(
    VkPhysicalDevice physicalDevice, VkFormat format,
    VkFormatProperties2KHR *pFormatProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceFormatProperties2KHR(unwrapped_phys_dev, format,
                                                pFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceFormatProperties2KHR(
    VkPhysicalDevice physicalDevice, VkFormat format,
    VkFormatProperties2KHR *pFormatProperties) {
    struct loader_physical_device_term *phys_dev_term =
        (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL == icd_term->GetPhysicalDeviceFormatProperties2KHR) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "ICD associated with VkPhysicalDevice does not support "
                   "vkGetPhysicalDeviceFormatProperties2KHR");
    }
    icd_term->GetPhysicalDeviceFormatProperties2KHR(phys_dev_term->phys_dev,
                                                    format, pFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties2KHR(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2KHR *pImageFormatInfo,
    VkImageFormatProperties2KHR *pImageFormatProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    return disp->GetPhysicalDeviceImageFormatProperties2KHR(
        unwrapped_phys_dev, pImageFormatInfo, pImageFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetPhysicalDeviceImageFormatProperties2KHR(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceImageFormatInfo2KHR *pImageFormatInfo,
    VkImageFormatProperties2KHR *pImageFormatProperties) {
    struct loader_physical_device_term *phys_dev_term =
        (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL == icd_term->GetPhysicalDeviceImageFormatProperties2KHR) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "ICD associated with VkPhysicalDevice does not support "
                   "vkGetPhysicalDeviceImageFormatProperties2KHR");
    }
    return icd_term->GetPhysicalDeviceImageFormatProperties2KHR(
        phys_dev_term->phys_dev, pImageFormatInfo, pImageFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties2KHR(
    VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2KHR *pQueueFamilyProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceQueueFamilyProperties2KHR(
        unwrapped_phys_dev, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

VKAPI_ATTR void VKAPI_CALL
terminator_GetPhysicalDeviceQueueFamilyProperties2KHR(
    VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties2KHR *pQueueFamilyProperties) {
    struct loader_physical_device_term *phys_dev_term =
        (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL == icd_term->GetPhysicalDeviceQueueFamilyProperties2KHR) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "ICD associated with VkPhysicalDevice does not support "
                   "vkGetPhysicalDeviceQueueFamilyProperties2KHR");
    }
    icd_term->GetPhysicalDeviceQueueFamilyProperties2KHR(
        phys_dev_term->phys_dev, pQueueFamilyPropertyCount,
        pQueueFamilyProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2KHR(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties2KHR *pMemoryProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceMemoryProperties2KHR(unwrapped_phys_dev,
                                                pMemoryProperties);
}
VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceMemoryProperties2KHR(
    VkPhysicalDevice physicalDevice,
    VkPhysicalDeviceMemoryProperties2KHR *pMemoryProperties) {
    struct loader_physical_device_term *phys_dev_term =
        (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL == icd_term->GetPhysicalDeviceMemoryProperties2KHR) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "ICD associated with VkPhysicalDevice does not support "
                   "vkGetPhysicalDeviceMemoryProperties2KHR");
    }
    icd_term->GetPhysicalDeviceMemoryProperties2KHR(phys_dev_term->phys_dev,
                                                    pMemoryProperties);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceSparseImageFormatProperties2KHR(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceSparseImageFormatInfo2KHR *pFormatInfo,
    uint32_t *pPropertyCount, VkSparseImageFormatProperties2KHR *pProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceSparseImageFormatProperties2KHR(
        unwrapped_phys_dev, pFormatInfo, pPropertyCount, pProperties);
}

VKAPI_ATTR void VKAPI_CALL
terminator_GetPhysicalDeviceSparseImageFormatProperties2KHR(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceSparseImageFormatInfo2KHR *pFormatInfo,
    uint32_t *pPropertyCount, VkSparseImageFormatProperties2KHR *pProperties) {
    struct loader_physical_device_term *phys_dev_term =
        (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL == icd_term->GetPhysicalDeviceSparseImageFormatProperties2KHR) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "ICD associated with VkPhysicalDevice does not support "
                   "vkGetPhysicalDeviceSparseImageFormatProperties2KHR");
    }
    icd_term->GetPhysicalDeviceSparseImageFormatProperties2KHR(
        phys_dev_term->phys_dev, pFormatInfo, pPropertyCount, pProperties);
}

// Definitions for the VK_KHR_maintenance1 extension

VKAPI_ATTR void VKAPI_CALL
vkTrimCommandPoolKHR(VkDevice device, VkCommandPool commandPool,
                     VkCommandPoolTrimFlagsKHR flags) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    disp->TrimCommandPoolKHR(device, commandPool, flags);
}

// Definitions for the VK_EXT_acquire_xlib_display extension

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
VKAPI_ATTR VkResult VKAPI_CALL vkAcquireXlibDisplayEXT(
    VkPhysicalDevice physicalDevice, Display *dpy, VkDisplayKHR display) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    return disp->AcquireXlibDisplayEXT(unwrapped_phys_dev, dpy, display);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_AcquireXlibDisplayEXT(
    VkPhysicalDevice physicalDevice, Display *dpy, VkDisplayKHR display) {
    struct loader_physical_device_term *phys_dev_term =
        (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL == icd_term->AcquireXlibDisplayEXT) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "ICD associated with VkPhysicalDevice does not support "
                   "vkAcquireXlibDisplayEXT");
    }
    return icd_term->AcquireXlibDisplayEXT(phys_dev_term->phys_dev, dpy,
                                           display);
}

VKAPI_ATTR VkResult VKAPI_CALL
vkGetRandROutputDisplayEXT(VkPhysicalDevice physicalDevice, Display *dpy,
                           RROutput rrOutput, VkDisplayKHR *pDisplay) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    return disp->GetRandROutputDisplayEXT(unwrapped_phys_dev, dpy, rrOutput,
                                          pDisplay);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_GetRandROutputDisplayEXT(
    VkPhysicalDevice physicalDevice, Display *dpy, RROutput rrOutput,
    VkDisplayKHR *pDisplay) {
    struct loader_physical_device_term *phys_dev_term =
        (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL == icd_term->GetRandROutputDisplayEXT) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "ICD associated with VkPhysicalDevice does not support "
                   "vkGetRandROutputDisplayEXT");
    }
    return icd_term->GetRandROutputDisplayEXT(phys_dev_term->phys_dev, dpy,
                                              rrOutput, pDisplay);
}
#endif /* VK_USE_PLATFORM_XLIB_XRANDR_EXT */

// Definitions for the VK_EXT_debug_marker extension commands which
// need to have a terminator function

VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectTagEXT(
    VkDevice device, VkDebugMarkerObjectTagInfoEXT *pTagInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    // If this is a physical device, we have to replace it with the proper one
    // for the next call.
    if (pTagInfo->objectType ==
        VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT) {
        struct loader_physical_device_tramp *phys_dev_tramp =
            (struct loader_physical_device_tramp *)(uintptr_t)pTagInfo->object;
        pTagInfo->object = (uint64_t)(uintptr_t)phys_dev_tramp->phys_dev;
    }
    return disp->DebugMarkerSetObjectTagEXT(device, pTagInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_DebugMarkerSetObjectTagEXT(
    VkDevice device, VkDebugMarkerObjectTagInfoEXT *pTagInfo) {
    uint32_t icd_index = 0;
    struct loader_device *dev;
    struct loader_icd_term *icd_term =
        loader_get_icd_and_device(device, &dev, &icd_index);
    if (NULL != icd_term && NULL != icd_term->DebugMarkerSetObjectTagEXT) {
        // If this is a physical device, we have to replace it with the proper
        // one for the next call.
        if (pTagInfo->objectType ==
            VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT) {
            struct loader_physical_device_term *phys_dev_term =
                (struct loader_physical_device_term *)(uintptr_t)
                    pTagInfo->object;
            pTagInfo->object = (uint64_t)(uintptr_t)phys_dev_term->phys_dev;

            // If this is a KHR_surface, and the ICD has created its own, we
            // have to replace it with the proper one for the next call.
        } else if (pTagInfo->objectType ==
                   VK_DEBUG_REPORT_OBJECT_TYPE_SURFACE_KHR_EXT) {
            if (NULL != icd_term && NULL != icd_term->CreateSwapchainKHR) {
                VkIcdSurface *icd_surface =
                    (VkIcdSurface *)(uintptr_t)pTagInfo->object;
                if (NULL != icd_surface->real_icd_surfaces) {
                    pTagInfo->object =
                        (uint64_t)icd_surface->real_icd_surfaces[icd_index];
                }
            }
        }
        return icd_term->DebugMarkerSetObjectTagEXT(device, pTagInfo);
    } else {
        return VK_SUCCESS;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectNameEXT(
    VkDevice device, VkDebugMarkerObjectNameInfoEXT *pNameInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    // If this is a physical device, we have to replace it with the proper one
    // for the next call.
    if (pNameInfo->objectType ==
        VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT) {
        struct loader_physical_device_tramp *phys_dev_tramp =
            (struct loader_physical_device_tramp *)(uintptr_t)pNameInfo->object;
        pNameInfo->object = (uint64_t)(uintptr_t)phys_dev_tramp->phys_dev;
    }
    return disp->DebugMarkerSetObjectNameEXT(device, pNameInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_DebugMarkerSetObjectNameEXT(
    VkDevice device, VkDebugMarkerObjectNameInfoEXT *pNameInfo) {
    uint32_t icd_index = 0;
    struct loader_device *dev;
    struct loader_icd_term *icd_term =
        loader_get_icd_and_device(device, &dev, &icd_index);
    if (NULL != icd_term && NULL != icd_term->DebugMarkerSetObjectNameEXT) {
        // If this is a physical device, we have to replace it with the proper
        // one for the next call.
        if (pNameInfo->objectType ==
            VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT) {
            struct loader_physical_device_term *phys_dev_term =
                (struct loader_physical_device_term *)(uintptr_t)
                    pNameInfo->object;
            pNameInfo->object = (uint64_t)(uintptr_t)phys_dev_term->phys_dev;

            // If this is a KHR_surface, and the ICD has created its own, we
            // have to replace it with the proper one for the next call.
        } else if (pNameInfo->objectType ==
                   VK_DEBUG_REPORT_OBJECT_TYPE_SURFACE_KHR_EXT) {
            if (NULL != icd_term && NULL != icd_term->CreateSwapchainKHR) {
                VkIcdSurface *icd_surface =
                    (VkIcdSurface *)(uintptr_t)pNameInfo->object;
                if (NULL != icd_surface->real_icd_surfaces) {
                    pNameInfo->object =
                        (uint64_t)(
                            uintptr_t)icd_surface->real_icd_surfaces[icd_index];
                }
            }
        }
        return icd_term->DebugMarkerSetObjectNameEXT(device, pNameInfo);
    } else {
        return VK_SUCCESS;
    }
}

// Definitions for the VK_EXT_direct_mode_display extension

VKAPI_ATTR VkResult VKAPI_CALL
vkReleaseDisplayEXT(VkPhysicalDevice physicalDevice, VkDisplayKHR display) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    return disp->ReleaseDisplayEXT(unwrapped_phys_dev, display);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_ReleaseDisplayEXT(
    VkPhysicalDevice physicalDevice, VkDisplayKHR display) {
    struct loader_physical_device_term *phys_dev_term =
        (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL == icd_term->ReleaseDisplayEXT) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "ICD associated with VkPhysicalDevice does not support "
                   "vkReleaseDisplayEXT");
    }
    return icd_term->ReleaseDisplayEXT(phys_dev_term->phys_dev, display);
}

// Definitions for the VK_EXT_display_surface_counter extension

VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilities2EXT(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilities2EXT *pSurfaceCapabilities) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    return disp->GetPhysicalDeviceSurfaceCapabilities2EXT(
        unwrapped_phys_dev, surface, pSurfaceCapabilities);
}

VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetPhysicalDeviceSurfaceCapabilities2EXT(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilities2EXT *pSurfaceCapabilities) {
    struct loader_physical_device_term *phys_dev_term =
        (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL != icd_term) {
        if (NULL == icd_term->GetPhysicalDeviceSurfaceCapabilities2EXT) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT,
                       0,
                       "ICD associated with VkPhysicalDevice does not support "
                       "vkGetPhysicalDeviceSurfaceCapabilities2EXT");
        }
        VkIcdSurface *icd_surface = (VkIcdSurface *)(surface);
        uint8_t icd_index = phys_dev_term->icd_index;
        if (NULL != icd_surface->real_icd_surfaces) {
            if (NULL != (void *)icd_surface->real_icd_surfaces[icd_index]) {
                return icd_term->GetPhysicalDeviceSurfaceCapabilities2EXT(
                    phys_dev_term->phys_dev,
                    icd_surface->real_icd_surfaces[icd_index],
                    pSurfaceCapabilities);
            }
        }
    }
    return icd_term->GetPhysicalDeviceSurfaceCapabilities2EXT(
        phys_dev_term->phys_dev, surface, pSurfaceCapabilities);
}

// Definitions for the VK_AMD_draw_indirect_count extension

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndirectCountAMD(
    VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
    VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
    uint32_t stride) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdDrawIndirectCountAMD(commandBuffer, buffer, offset, countBuffer,
                                  countBufferOffset, maxDrawCount, stride);
}

VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexedIndirectCountAMD(
    VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset,
    VkBuffer countBuffer, VkDeviceSize countBufferOffset, uint32_t maxDrawCount,
    uint32_t stride) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdDrawIndexedIndirectCountAMD(commandBuffer, buffer, offset,
                                         countBuffer, countBufferOffset,
                                         maxDrawCount, stride);
}

// Definitions for the VK_NV_external_memory_capabilities extension

VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceExternalImageFormatPropertiesNV(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
    VkExternalMemoryHandleTypeFlagsNV externalHandleType,
    VkExternalImageFormatPropertiesNV *pExternalImageFormatProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);

    return disp->GetPhysicalDeviceExternalImageFormatPropertiesNV(
        unwrapped_phys_dev, format, type, tiling, usage, flags,
        externalHandleType, pExternalImageFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetPhysicalDeviceExternalImageFormatPropertiesNV(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
    VkExternalMemoryHandleTypeFlagsNV externalHandleType,
    VkExternalImageFormatPropertiesNV *pExternalImageFormatProperties) {
    struct loader_physical_device_term *phys_dev_term =
        (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    if (!icd_term->GetPhysicalDeviceExternalImageFormatPropertiesNV) {
        if (externalHandleType) {
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        }

        if (!icd_term->GetPhysicalDeviceImageFormatProperties) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        pExternalImageFormatProperties->externalMemoryFeatures = 0;
        pExternalImageFormatProperties->exportFromImportedHandleTypes = 0;
        pExternalImageFormatProperties->compatibleHandleTypes = 0;

        return icd_term->GetPhysicalDeviceImageFormatProperties(
            phys_dev_term->phys_dev, format, type, tiling, usage, flags,
            &pExternalImageFormatProperties->imageFormatProperties);
    }

    return icd_term->GetPhysicalDeviceExternalImageFormatPropertiesNV(
        phys_dev_term->phys_dev, format, type, tiling, usage, flags,
        externalHandleType, pExternalImageFormatProperties);
}

#ifdef VK_USE_PLATFORM_WIN32_KHR

// Definitions for the VK_NV_external_memory_win32 extension

VKAPI_ATTR VkResult VKAPI_CALL vkGetMemoryWin32HandleNV(
    VkDevice device, VkDeviceMemory memory,
    VkExternalMemoryHandleTypeFlagsNV handleType, HANDLE *pHandle) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetMemoryWin32HandleNV(device, memory, handleType, pHandle);
}

#endif // VK_USE_PLATFORM_WIN32_KHR

// Definitions for the VK_NVX_device_generated_commands

VKAPI_ATTR void VKAPI_CALL vkCmdProcessCommandsNVX(
    VkCommandBuffer commandBuffer,
    const VkCmdProcessCommandsInfoNVX *pProcessCommandsInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdProcessCommandsNVX(commandBuffer, pProcessCommandsInfo);
}

VKAPI_ATTR void VKAPI_CALL vkCmdReserveSpaceForCommandsNVX(
    VkCommandBuffer commandBuffer,
    const VkCmdReserveSpaceForCommandsInfoNVX *pReserveSpaceInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdReserveSpaceForCommandsNVX(commandBuffer, pReserveSpaceInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateIndirectCommandsLayoutNVX(
    VkDevice device, const VkIndirectCommandsLayoutCreateInfoNVX *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkIndirectCommandsLayoutNVX *pIndirectCommandsLayout) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->CreateIndirectCommandsLayoutNVX(
        device, pCreateInfo, pAllocator, pIndirectCommandsLayout);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyIndirectCommandsLayoutNVX(
    VkDevice device, VkIndirectCommandsLayoutNVX indirectCommandsLayout,
    const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    disp->DestroyIndirectCommandsLayoutNVX(device, indirectCommandsLayout,
                                           pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateObjectTableNVX(
    VkDevice device, const VkObjectTableCreateInfoNVX *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkObjectTableNVX *pObjectTable) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->CreateObjectTableNVX(device, pCreateInfo, pAllocator,
                                      pObjectTable);
}

VKAPI_ATTR void VKAPI_CALL
vkDestroyObjectTableNVX(VkDevice device, VkObjectTableNVX objectTable,
                        const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    disp->DestroyObjectTableNVX(device, objectTable, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL vkRegisterObjectsNVX(
    VkDevice device, VkObjectTableNVX objectTable, uint32_t objectCount,
    const VkObjectTableEntryNVX *const *ppObjectTableEntries,
    const uint32_t *pObjectIndices) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->RegisterObjectsNVX(device, objectTable, objectCount,
                                    ppObjectTableEntries, pObjectIndices);
}

VKAPI_ATTR VkResult VKAPI_CALL vkUnregisterObjectsNVX(
    VkDevice device, VkObjectTableNVX objectTable, uint32_t objectCount,
    const VkObjectEntryTypeNVX *pObjectEntryTypes,
    const uint32_t *pObjectIndices) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->UnregisterObjectsNVX(device, objectTable, objectCount,
                                      pObjectEntryTypes, pObjectIndices);
}

VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX(
    VkPhysicalDevice physicalDevice,
    VkDeviceGeneratedCommandsFeaturesNVX *pFeatures,
    VkDeviceGeneratedCommandsLimitsNVX *pLimits) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceGeneratedCommandsPropertiesNVX(unwrapped_phys_dev,
                                                          pFeatures, pLimits);
}

VKAPI_ATTR void VKAPI_CALL
terminator_GetPhysicalDeviceGeneratedCommandsPropertiesNVX(
    VkPhysicalDevice physicalDevice,
    VkDeviceGeneratedCommandsFeaturesNVX *pFeatures,
    VkDeviceGeneratedCommandsLimitsNVX *pLimits) {
    struct loader_physical_device_term *phys_dev_term =
        (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL == icd_term->GetPhysicalDeviceGeneratedCommandsPropertiesNVX) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "ICD associated with VkPhysicalDevice does not support "
                   "vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX");
    } else {
        icd_term->GetPhysicalDeviceGeneratedCommandsPropertiesNVX(
            phys_dev_term->phys_dev, pFeatures, pLimits);
    }
}

// GPA helpers for extensions

bool extension_instance_gpa(struct loader_instance *ptr_instance,
                            const char *name, void **addr) {
    *addr = NULL;

    // Functions for the VK_KHR_get_physical_device_properties2 extension

    if (!strcmp("vkGetPhysicalDeviceFeatures2KHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions
                     .khr_get_physical_device_properties2 == 1)
                    ? (void *)vkGetPhysicalDeviceFeatures2KHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceProperties2KHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions
                     .khr_get_physical_device_properties2 == 1)
                    ? (void *)vkGetPhysicalDeviceProperties2KHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceFormatProperties2KHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions
                     .khr_get_physical_device_properties2 == 1)
                    ? (void *)vkGetPhysicalDeviceFormatProperties2KHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceImageFormatProperties2KHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions
                     .khr_get_physical_device_properties2 == 1)
                    ? (void *)vkGetPhysicalDeviceImageFormatProperties2KHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceQueueFamilyProperties2KHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions
                     .khr_get_physical_device_properties2 == 1)
                    ? (void *)vkGetPhysicalDeviceQueueFamilyProperties2KHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceMemoryProperties2KHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions
                     .khr_get_physical_device_properties2 == 1)
                    ? (void *)vkGetPhysicalDeviceMemoryProperties2KHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceSparseImageFormatProperties2KHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions
                     .khr_get_physical_device_properties2 == 1)
                    ? (void *)vkGetPhysicalDeviceSparseImageFormatProperties2KHR
                    : NULL;
        return true;
    }

    // Functions for the VK_KHR_maintenance1 extension

    if (!strcmp("vkTrimCommandPoolKHR", name)) {
        *addr = (void *)vkTrimCommandPoolKHR;
        return true;
    }

// Functions for the VK_EXT_acquire_xlib_display extension

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    if (!strcmp("vkAcquireXlibDisplayEXT", name)) {
        *addr =
            (ptr_instance->enabled_known_extensions.ext_acquire_xlib_display ==
             1)
                ? (void *)vkAcquireXlibDisplayEXT
                : NULL;
        return true;
    }

    if (!strcmp("vkGetRandROutputDisplayEXT", name)) {
        *addr =
            (ptr_instance->enabled_known_extensions.ext_acquire_xlib_display ==
             1)
                ? (void *)vkGetRandROutputDisplayEXT
                : NULL;
        return true;
    }

#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT

    // Definitions for the VK_EXT_debug_marker extension commands which
    // need to have a terminator function.  Since these are device
    // commands, we always need to return a valid value for them.

    if (!strcmp("vkDebugMarkerSetObjectTagEXT", name)) {
        *addr = (void *)vkDebugMarkerSetObjectTagEXT;
        return true;
    }
    if (!strcmp("vkDebugMarkerSetObjectNameEXT", name)) {
        *addr = (void *)vkDebugMarkerSetObjectNameEXT;
        return true;
    }

    // Functions for the VK_EXT_direct_mode_display extension

    if (!strcmp("vkReleaseDisplayEXT", name)) {
        *addr =
            (ptr_instance->enabled_known_extensions.ext_direct_mode_display ==
             1)
                ? (void *)vkReleaseDisplayEXT
                : NULL;
        return true;
    }

    // Functions for the VK_EXT_display_surface_counter extension

    if (!strcmp("vkGetPhysicalDeviceSurfaceCapabilities2EXT", name)) {
        *addr = (ptr_instance->enabled_known_extensions
                     .ext_display_surface_counter == 1)
                    ? (void *)vkGetPhysicalDeviceSurfaceCapabilities2EXT
                    : NULL;
        return true;
    }

    // Functions for the VK_AMD_draw_indirect_count extension

    if (!strcmp("vkCmdDrawIndirectCountAMD", name)) {
        *addr = (void *)vkCmdDrawIndirectCountAMD;
        return true;
    }
    if (!strcmp("vkCmdDrawIndexedIndirectCountAMD", name)) {
        *addr = (void *)vkCmdDrawIndexedIndirectCountAMD;
        return true;
    }
    // Functions for the VK_NV_external_memory_capabilities extension

    if (!strcmp("vkGetPhysicalDeviceExternalImageFormatPropertiesNV", name)) {
        *addr = (ptr_instance->enabled_known_extensions
                     .nv_external_memory_capabilities == 1)
                    ? (void *)vkGetPhysicalDeviceExternalImageFormatPropertiesNV
                    : NULL;
        return true;
    }

#ifdef VK_USE_PLATFORM_WIN32_KHR

    // Functions for the VK_NV_external_memory_win32 extension
    if (!strcmp("vkGetMemoryWin32HandleNV", name)) {
        *addr = (void *)vkGetMemoryWin32HandleNV;
        return true;
    }

#endif // VK_USE_PLATFORM_WIN32_KHR

    // Functions for the VK_NVX_device_generated_commands extension

    if (!strcmp("vkCmdProcessCommandsNVX", name)) {
        *addr = (void *)vkCmdProcessCommandsNVX;
        return true;
    }
    if (!strcmp("vkCmdReserveSpaceForCommandsNVX", name)) {
        *addr = (void *)vkCmdReserveSpaceForCommandsNVX;
        return true;
    }
    if (!strcmp("vkCreateIndirectCommandsLayoutNVX", name)) {
        *addr = (void *)vkCreateIndirectCommandsLayoutNVX;
        return true;
    }
    if (!strcmp("vkDestroyIndirectCommandsLayoutNVX", name)) {
        *addr = (void *)vkDestroyIndirectCommandsLayoutNVX;
        return true;
    }
    if (!strcmp("vkCreateObjectTableNVX", name)) {
        *addr = (void *)vkCreateObjectTableNVX;
        return true;
    }
    if (!strcmp("vkDestroyObjectTableNVX", name)) {
        *addr = (void *)vkDestroyObjectTableNVX;
        return true;
    }
    if (!strcmp("vkRegisterObjectsNVX", name)) {
        *addr = (void *)vkRegisterObjectsNVX;
        return true;
    }
    if (!strcmp("vkUnregisterObjectsNVX", name)) {
        *addr = (void *)vkUnregisterObjectsNVX;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX", name)) {
        *addr = (void *)vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX;
        return true;
    }

    return false;
}

void extensions_create_instance(struct loader_instance *ptr_instance,
                                const VkInstanceCreateInfo *pCreateInfo) {
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        if (0 ==
            strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                   VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions
                .khr_get_physical_device_properties2 = 1;
#ifdef VK_USE_PLATFORM_XLIB_KHR
        } else if (0 == strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                               VK_EXT_ACQUIRE_XLIB_DISPLAY_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions.ext_acquire_xlib_display = 1;
#endif
        } else if (0 == strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                               VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions.ext_direct_mode_display = 1;
        } else if (0 == strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                               VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions.ext_display_surface_counter =
                1;
        } else if (0 ==
                   strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                          VK_NV_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions
                .nv_external_memory_capabilities = 1;
        }
    }
}
