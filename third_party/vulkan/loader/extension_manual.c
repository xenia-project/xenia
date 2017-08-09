/*
 * Copyright (c) 2015-2017 The Khronos Group Inc.
 * Copyright (c) 2015-2017 Valve Corporation
 * Copyright (c) 2015-2017 LunarG, Inc.
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
 * Author: Mark Young <marky@lunarg.com>
 * Author: Lenny Komow <lenny@lunarg.com>
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vk_loader_platform.h"
#include "loader.h"
#include "vk_loader_extensions.h"
#include <vulkan/vk_icd.h>
#include "wsi.h"
#include "debug_report.h"

// ---- Manually added trampoline/terminator functions

// These functions, for whatever reason, require more complex changes than
// can easily be automatically generated.
VkResult setupLoaderTrampPhysDevGroups(VkInstance instance);
VkResult setupLoaderTermPhysDevGroups(struct loader_instance *inst);

// ---- VK_KHX_device_group extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL EnumeratePhysicalDeviceGroupsKHX(
    VkInstance instance, uint32_t *pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupPropertiesKHX *pPhysicalDeviceGroupProperties) {
    VkResult res = VK_SUCCESS;
    uint32_t count;
    uint32_t i;
    struct loader_instance *inst = NULL;

    loader_platform_thread_lock_mutex(&loader_lock);

    inst = loader_get_instance(instance);
    if (NULL == inst) {
        res = VK_ERROR_INITIALIZATION_FAILED;
        goto out;
    }

    if (NULL == pPhysicalDeviceGroupCount) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "vkEnumeratePhysicalDeviceGroupsKHX: Received NULL pointer for physical "
                   "device group count return value.");
        res = VK_ERROR_INITIALIZATION_FAILED;
        goto out;
    }

    VkResult setup_res = setupLoaderTrampPhysDevGroups(instance);
    if (VK_SUCCESS != setup_res) {
        res = setup_res;
        goto out;
    }

    count = inst->phys_dev_group_count_tramp;

    // Wrap the PhysDev object for loader usage, return wrapped objects
    if (NULL != pPhysicalDeviceGroupProperties) {
        if (inst->phys_dev_group_count_tramp > *pPhysicalDeviceGroupCount) {
            loader_log(inst, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                       "vkEnumeratePhysicalDeviceGroupsKHX: Trimming device group count down"
                       " by application request from %d to %d physical device groups",
                       inst->phys_dev_group_count_tramp, *pPhysicalDeviceGroupCount);
            count = *pPhysicalDeviceGroupCount;
            res = VK_INCOMPLETE;
        }
        for (i = 0; i < count; i++) {
            memcpy(&pPhysicalDeviceGroupProperties[i], inst->phys_dev_groups_tramp[i],
                   sizeof(VkPhysicalDeviceGroupPropertiesKHX));
        }
    }

    *pPhysicalDeviceGroupCount = count;

out:

    loader_platform_thread_unlock_mutex(&loader_lock);
    return res;
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_EnumeratePhysicalDeviceGroupsKHX(
    VkInstance instance, uint32_t *pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupPropertiesKHX *pPhysicalDeviceGroupProperties) {
    struct loader_instance *inst = (struct loader_instance *)instance;
    VkResult res = VK_SUCCESS;

    // Always call the setup loader terminator physical device groups because they may
    // have changed at any point.
    res = setupLoaderTermPhysDevGroups(inst);
    if (VK_SUCCESS != res) {
        goto out;
    }

    uint32_t copy_count = inst->phys_dev_group_count_term;
    if (NULL != pPhysicalDeviceGroupProperties) {
        if (copy_count > *pPhysicalDeviceGroupCount) {
            copy_count = *pPhysicalDeviceGroupCount;
            res = VK_INCOMPLETE;
        }

        for (uint32_t i = 0; i < copy_count; i++) {
            memcpy(&pPhysicalDeviceGroupProperties[i], inst->phys_dev_groups_term[i],
                   sizeof(VkPhysicalDeviceGroupPropertiesKHX));
        }
    }

    *pPhysicalDeviceGroupCount = copy_count;

out:

    return res;
}

// ---- VK_NV_external_memory_capabilities extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL
GetPhysicalDeviceExternalImageFormatPropertiesNV(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
    VkExternalMemoryHandleTypeFlagsNV externalHandleType,
    VkExternalImageFormatPropertiesNV *pExternalImageFormatProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
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

    if (!icd_term->dispatch.GetPhysicalDeviceExternalImageFormatPropertiesNV) {
        if (externalHandleType) {
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        }

        if (!icd_term->dispatch.GetPhysicalDeviceImageFormatProperties) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        pExternalImageFormatProperties->externalMemoryFeatures = 0;
        pExternalImageFormatProperties->exportFromImportedHandleTypes = 0;
        pExternalImageFormatProperties->compatibleHandleTypes = 0;

        return icd_term->dispatch.GetPhysicalDeviceImageFormatProperties(
            phys_dev_term->phys_dev, format, type, tiling, usage, flags,
            &pExternalImageFormatProperties->imageFormatProperties);
    }

    return icd_term->dispatch.GetPhysicalDeviceExternalImageFormatPropertiesNV(
        phys_dev_term->phys_dev, format, type, tiling, usage, flags,
        externalHandleType, pExternalImageFormatProperties);
}

// ---- VK_KHR_get_physical_device_properties2 extension trampoline/terminators

VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2KHR *pFeatures) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceFeatures2KHR(unwrapped_phys_dev, pFeatures);
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceFeatures2KHR(VkPhysicalDevice physicalDevice,
                                                                    VkPhysicalDeviceFeatures2KHR *pFeatures) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    if (icd_term->dispatch.GetPhysicalDeviceFeatures2KHR != NULL) {
        // Pass the call to the driver
        icd_term->dispatch.GetPhysicalDeviceFeatures2KHR(phys_dev_term->phys_dev, pFeatures);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceFeatures2KHR: Emulating call in ICD \"%s\" using vkGetPhysicalDeviceFeatures",
                   icd_term->scanned_icd->lib_name);

        // Write to the VkPhysicalDeviceFeatures2KHR struct
        icd_term->dispatch.GetPhysicalDeviceFeatures(phys_dev_term->phys_dev, &pFeatures->features);

        void *pNext = pFeatures->pNext;
        while (pNext != NULL) {
            switch (*(VkStructureType *)pNext) {
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES_KHX: {
                    // Skip the check if VK_KHX_multiview is enabled because it's a device extension
                    // Write to the VkPhysicalDeviceMultiviewFeaturesKHX struct
                    VkPhysicalDeviceMultiviewFeaturesKHX *multiview_features = pNext;
                    multiview_features->multiview = VK_FALSE;
                    multiview_features->multiviewGeometryShader = VK_FALSE;
                    multiview_features->multiviewTessellationShader = VK_FALSE;

                    pNext = multiview_features->pNext;
                    break;
                }
                default: {
                    loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                               "vkGetPhysicalDeviceFeatures2KHR: Emulation found unrecognized structure type in pFeatures->pNext - "
                               "this struct will be ignored");

                    struct VkStructureHeader *header = pNext;
                    pNext = (void *)header->pNext;
                    break;
                }
            }
        }
    }
}

VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice,
                                                           VkPhysicalDeviceProperties2KHR *pProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceProperties2KHR(unwrapped_phys_dev, pProperties);
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceProperties2KHR(VkPhysicalDevice physicalDevice,
                                                                      VkPhysicalDeviceProperties2KHR *pProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    if (icd_term->dispatch.GetPhysicalDeviceProperties2KHR != NULL) {
        // Pass the call to the driver
        icd_term->dispatch.GetPhysicalDeviceProperties2KHR(phys_dev_term->phys_dev, pProperties);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceProperties2KHR: Emulating call in ICD \"%s\" using vkGetPhysicalDeviceProperties",
                   icd_term->scanned_icd->lib_name);

        // Write to the VkPhysicalDeviceProperties2KHR struct
        icd_term->dispatch.GetPhysicalDeviceProperties(phys_dev_term->phys_dev, &pProperties->properties);

        void *pNext = pProperties->pNext;
        while (pNext != NULL) {
            switch (*(VkStructureType *)pNext) {
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES_KHR: {
                    VkPhysicalDeviceIDPropertiesKHR *id_properties = pNext;

                    // Verify that "VK_KHR_external_memory_capabilities" is enabled
                    if (icd_term->this_instance->enabled_known_extensions.khr_external_memory_capabilities) {
                        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                                   "vkGetPhysicalDeviceProperties2KHR: Emulation cannot generate unique IDs for struct "
                                   "VkPhysicalDeviceIDPropertiesKHR - setting IDs to zero instead");

                        // Write to the VkPhysicalDeviceIDPropertiesKHR struct
                        memset(id_properties->deviceUUID, 0, VK_UUID_SIZE);
                        memset(id_properties->driverUUID, 0, VK_UUID_SIZE);
                        id_properties->deviceLUIDValid = VK_FALSE;
                    }

                    pNext = id_properties->pNext;
                    break;
                }
                default: {
                    loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                               "vkGetPhysicalDeviceProperties2KHR: Emulation found unrecognized structure type in "
                               "pProperties->pNext - this struct will be ignored");

                    struct VkStructureHeader *header = pNext;
                    pNext = (void *)header->pNext;
                    break;
                }
            }
        }
    }
}

VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice physicalDevice, VkFormat format,
                                                                 VkFormatProperties2KHR *pFormatProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceFormatProperties2KHR(unwrapped_phys_dev, format, pFormatProperties);
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice physicalDevice, VkFormat format,
                                                                            VkFormatProperties2KHR *pFormatProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    if (icd_term->dispatch.GetPhysicalDeviceFormatProperties2KHR != NULL) {
        // Pass the call to the driver
        icd_term->dispatch.GetPhysicalDeviceFormatProperties2KHR(phys_dev_term->phys_dev, format, pFormatProperties);
    } else {
        // Emulate the call
        loader_log(
            icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
            "vkGetPhysicalDeviceFormatProperties2KHR: Emulating call in ICD \"%s\" using vkGetPhysicalDeviceFormatProperties",
            icd_term->scanned_icd->lib_name);

        // Write to the VkFormatProperties2KHR struct
        icd_term->dispatch.GetPhysicalDeviceFormatProperties(phys_dev_term->phys_dev, format, &pFormatProperties->formatProperties);

        if (pFormatProperties->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceFormatProperties2KHR: Emulation found unrecognized structure type in "
                       "pFormatProperties->pNext - this struct will be ignored");
        }
    }
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceImageFormatProperties2KHR(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2KHR *pImageFormatInfo,
    VkImageFormatProperties2KHR *pImageFormatProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    return disp->GetPhysicalDeviceImageFormatProperties2KHR(unwrapped_phys_dev, pImageFormatInfo, pImageFormatProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_GetPhysicalDeviceImageFormatProperties2KHR(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceImageFormatInfo2KHR *pImageFormatInfo,
    VkImageFormatProperties2KHR *pImageFormatProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    if (icd_term->dispatch.GetPhysicalDeviceImageFormatProperties2KHR != NULL) {
        // Pass the call to the driver
        return icd_term->dispatch.GetPhysicalDeviceImageFormatProperties2KHR(phys_dev_term->phys_dev, pImageFormatInfo,
                                                                             pImageFormatProperties);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceImageFormatProperties2KHR: Emulating call in ICD \"%s\" using "
                   "vkGetPhysicalDeviceImageFormatProperties",
                   icd_term->scanned_icd->lib_name);

        // If there is more info in  either pNext, then this is unsupported
        if (pImageFormatInfo->pNext != NULL || pImageFormatProperties->pNext != NULL) {
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        }

        // Write to the VkImageFormatProperties2KHR struct
        return icd_term->dispatch.GetPhysicalDeviceImageFormatProperties(
            phys_dev_term->phys_dev, pImageFormatInfo->format, pImageFormatInfo->type, pImageFormatInfo->tiling,
            pImageFormatInfo->usage, pImageFormatInfo->flags, &pImageFormatProperties->imageFormatProperties);
    }
}

VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice physicalDevice,
                                                                      uint32_t *pQueueFamilyPropertyCount,
                                                                      VkQueueFamilyProperties2KHR *pQueueFamilyProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceQueueFamilyProperties2KHR(unwrapped_phys_dev, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceQueueFamilyProperties2KHR(
    VkPhysicalDevice physicalDevice, uint32_t *pQueueFamilyPropertyCount, VkQueueFamilyProperties2KHR *pQueueFamilyProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    if (icd_term->dispatch.GetPhysicalDeviceQueueFamilyProperties2KHR != NULL) {
        // Pass the call to the driver
        icd_term->dispatch.GetPhysicalDeviceQueueFamilyProperties2KHR(phys_dev_term->phys_dev, pQueueFamilyPropertyCount,
                                                                      pQueueFamilyProperties);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceQueueFamilyProperties2KHR: Emulating call in ICD \"%s\" using "
                   "vkGetPhysicalDeviceQueueFamilyProperties",
                   icd_term->scanned_icd->lib_name);

        if (pQueueFamilyProperties == NULL || *pQueueFamilyPropertyCount == 0) {
            // Write to pQueueFamilyPropertyCount
            icd_term->dispatch.GetPhysicalDeviceQueueFamilyProperties(phys_dev_term->phys_dev, pQueueFamilyPropertyCount, NULL);
        } else {
            // Allocate a temporary array for the output of the old function
            VkQueueFamilyProperties *properties = loader_stack_alloc(*pQueueFamilyPropertyCount * sizeof(VkQueueFamilyProperties));
            if (properties == NULL) {
                *pQueueFamilyPropertyCount = 0;
                loader_log(
                    icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                    "vkGetPhysicalDeviceQueueFamilyProperties2KHR: Out of memory - Failed to allocate array for loader emulation.");
                return;
            }

            icd_term->dispatch.GetPhysicalDeviceQueueFamilyProperties(phys_dev_term->phys_dev, pQueueFamilyPropertyCount,
                                                                      properties);
            for (uint32_t i = 0; i < *pQueueFamilyPropertyCount; ++i) {
                // Write to the VkQueueFamilyProperties2KHR struct
                memcpy(&pQueueFamilyProperties[i].queueFamilyProperties, &properties[i], sizeof(VkQueueFamilyProperties));

                if (pQueueFamilyProperties[i].pNext != NULL) {
                    loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                               "vkGetPhysicalDeviceQueueFamilyProperties2KHR: Emulation found unrecognized structure type in "
                               "pQueueFamilyProperties[%d].pNext - this struct will be ignored",
                               i);
                }
            }
        }
    }
}

VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice physicalDevice,
                                                                 VkPhysicalDeviceMemoryProperties2KHR *pMemoryProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceMemoryProperties2KHR(unwrapped_phys_dev, pMemoryProperties);
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceMemoryProperties2KHR(
    VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties2KHR *pMemoryProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    if (icd_term->dispatch.GetPhysicalDeviceMemoryProperties2KHR != NULL) {
        // Pass the call to the driver
        icd_term->dispatch.GetPhysicalDeviceMemoryProperties2KHR(phys_dev_term->phys_dev, pMemoryProperties);
    } else {
        // Emulate the call
        loader_log(
            icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
            "vkGetPhysicalDeviceMemoryProperties2KHR: Emulating call in ICD \"%s\" using vkGetPhysicalDeviceMemoryProperties",
            icd_term->scanned_icd->lib_name);

        // Write to the VkPhysicalDeviceMemoryProperties2KHR struct
        icd_term->dispatch.GetPhysicalDeviceMemoryProperties(phys_dev_term->phys_dev, &pMemoryProperties->memoryProperties);

        if (pMemoryProperties->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceMemoryProperties2KHR: Emulation found unrecognized structure type in "
                       "pMemoryProperties->pNext - this struct will be ignored");
        }
    }
}

VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceSparseImageFormatProperties2KHR(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2KHR *pFormatInfo, uint32_t *pPropertyCount,
    VkSparseImageFormatProperties2KHR *pProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceSparseImageFormatProperties2KHR(unwrapped_phys_dev, pFormatInfo, pPropertyCount, pProperties);
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceSparseImageFormatProperties2KHR(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSparseImageFormatInfo2KHR *pFormatInfo, uint32_t *pPropertyCount,
    VkSparseImageFormatProperties2KHR *pProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    if (icd_term->dispatch.GetPhysicalDeviceSparseImageFormatProperties2KHR != NULL) {
        // Pass the call to the driver
        icd_term->dispatch.GetPhysicalDeviceSparseImageFormatProperties2KHR(phys_dev_term->phys_dev, pFormatInfo, pPropertyCount,
                                                                            pProperties);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceSparseImageFormatProperties2KHR: Emulating call in ICD \"%s\" using "
                   "vkGetPhysicalDeviceSparseImageFormatProperties",
                   icd_term->scanned_icd->lib_name);

        if (pFormatInfo->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceSparseImageFormatProperties2KHR: Emulation found unrecognized structure type in "
                       "pFormatInfo->pNext - this struct will be ignored");
        }

        if (pProperties == NULL || *pPropertyCount == 0) {
            // Write to pPropertyCount
            icd_term->dispatch.GetPhysicalDeviceSparseImageFormatProperties(
                phys_dev_term->phys_dev, pFormatInfo->format, pFormatInfo->type, pFormatInfo->samples, pFormatInfo->usage,
                pFormatInfo->tiling, pPropertyCount, NULL);
        } else {
            // Allocate a temporary array for the output of the old function
            VkSparseImageFormatProperties *properties =
                loader_stack_alloc(*pPropertyCount * sizeof(VkSparseImageMemoryRequirements));
            if (properties == NULL) {
                *pPropertyCount = 0;
                loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "vkGetPhysicalDeviceSparseImageFormatProperties2KHR: Out of memory - Failed to allocate array for "
                           "loader emulation.");
                return;
            }

            icd_term->dispatch.GetPhysicalDeviceSparseImageFormatProperties(
                phys_dev_term->phys_dev, pFormatInfo->format, pFormatInfo->type, pFormatInfo->samples, pFormatInfo->usage,
                pFormatInfo->tiling, pPropertyCount, properties);
            for (uint32_t i = 0; i < *pPropertyCount; ++i) {
                // Write to the VkSparseImageFormatProperties2KHR struct
                memcpy(&pProperties[i].properties, &properties[i], sizeof(VkSparseImageFormatProperties));

                if (pProperties[i].pNext != NULL) {
                    loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                               "vkGetPhysicalDeviceSparseImageFormatProperties2KHR: Emulation found unrecognized structure type in "
                               "pProperties[%d].pNext - this struct will be ignored",
                               i);
                }
            }
        }
    }
}

// ---- VK_KHR_get_surface_capabilities2 extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceCapabilities2KHR(VkPhysicalDevice physicalDevice,
                                                                        const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo,
                                                                        VkSurfaceCapabilities2KHR *pSurfaceCapabilities) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    return disp->GetPhysicalDeviceSurfaceCapabilities2KHR(unwrapped_phys_dev, pSurfaceInfo, pSurfaceCapabilities);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_GetPhysicalDeviceSurfaceCapabilities2KHR(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo,
    VkSurfaceCapabilities2KHR *pSurfaceCapabilities) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    VkIcdSurface *icd_surface = (VkIcdSurface *)(pSurfaceInfo->surface);
    uint8_t icd_index = phys_dev_term->icd_index;

    if (icd_term->dispatch.GetPhysicalDeviceSurfaceCapabilities2KHR != NULL) {
        // Pass the call to the driver, possibly unwrapping the ICD surface
        if (icd_surface->real_icd_surfaces != NULL && (void *)icd_surface->real_icd_surfaces[icd_index] != NULL) {
            VkPhysicalDeviceSurfaceInfo2KHR info_copy = *pSurfaceInfo;
            info_copy.surface = icd_surface->real_icd_surfaces[icd_index];
            return icd_term->dispatch.GetPhysicalDeviceSurfaceCapabilities2KHR(phys_dev_term->phys_dev, &info_copy,
                                                                               pSurfaceCapabilities);
        } else {
            return icd_term->dispatch.GetPhysicalDeviceSurfaceCapabilities2KHR(phys_dev_term->phys_dev, pSurfaceInfo,
                                                                               pSurfaceCapabilities);
        }
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceSurfaceCapabilities2KHR: Emulating call in ICD \"%s\" using "
                   "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
                   icd_term->scanned_icd->lib_name);

        if (pSurfaceInfo->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceSurfaceCapabilities2KHR: Emulation found unrecognized structure type in "
                       "pSurfaceInfo->pNext - this struct will be ignored");
        }

        // Write to the VkSurfaceCapabilities2KHR struct
        VkSurfaceKHR surface = pSurfaceInfo->surface;
        if (icd_surface->real_icd_surfaces != NULL && (void *)icd_surface->real_icd_surfaces[icd_index] != NULL) {
            surface = icd_surface->real_icd_surfaces[icd_index];
        }
        VkResult res = icd_term->dispatch.GetPhysicalDeviceSurfaceCapabilitiesKHR(phys_dev_term->phys_dev, surface,
                                                                                  &pSurfaceCapabilities->surfaceCapabilities);

        if (pSurfaceCapabilities->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceSurfaceCapabilities2KHR: Emulation found unrecognized structure type in "
                       "pSurfaceCapabilities->pNext - this struct will be ignored");
        }
        return res;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice,
                                                                   const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo,
                                                                   uint32_t *pSurfaceFormatCount,
                                                                   VkSurfaceFormat2KHR *pSurfaceFormats) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    return disp->GetPhysicalDeviceSurfaceFormats2KHR(unwrapped_phys_dev, pSurfaceInfo, pSurfaceFormatCount, pSurfaceFormats);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_GetPhysicalDeviceSurfaceFormats2KHR(VkPhysicalDevice physicalDevice,
                                                                              const VkPhysicalDeviceSurfaceInfo2KHR *pSurfaceInfo,
                                                                              uint32_t *pSurfaceFormatCount,
                                                                              VkSurfaceFormat2KHR *pSurfaceFormats) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    VkIcdSurface *icd_surface = (VkIcdSurface *)(pSurfaceInfo->surface);
    uint8_t icd_index = phys_dev_term->icd_index;

    if (icd_term->dispatch.GetPhysicalDeviceSurfaceFormats2KHR != NULL) {
        // Pass the call to the driver, possibly unwrapping the ICD surface
        if (icd_surface->real_icd_surfaces != NULL && (void *)icd_surface->real_icd_surfaces[icd_index] != NULL) {
            VkPhysicalDeviceSurfaceInfo2KHR info_copy = *pSurfaceInfo;
            info_copy.surface = icd_surface->real_icd_surfaces[icd_index];
            return icd_term->dispatch.GetPhysicalDeviceSurfaceFormats2KHR(phys_dev_term->phys_dev, &info_copy, pSurfaceFormatCount,
                                                                          pSurfaceFormats);
        } else {
            return icd_term->dispatch.GetPhysicalDeviceSurfaceFormats2KHR(phys_dev_term->phys_dev, pSurfaceInfo,
                                                                          pSurfaceFormatCount, pSurfaceFormats);
        }
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceSurfaceFormats2KHR: Emulating call in ICD \"%s\" using vkGetPhysicalDeviceSurfaceFormatsKHR",
                   icd_term->scanned_icd->lib_name);

        if (pSurfaceInfo->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceSurfaceFormats2KHR: Emulation found unrecognized structure type in pSurfaceInfo->pNext "
                       "- this struct will be ignored");
        }

        VkSurfaceKHR surface = pSurfaceInfo->surface;
        if (icd_surface->real_icd_surfaces != NULL && (void *)icd_surface->real_icd_surfaces[icd_index] != NULL) {
            surface = icd_surface->real_icd_surfaces[icd_index];
        }

        if (*pSurfaceFormatCount == 0 || pSurfaceFormats == NULL) {
            // Write to pSurfaceFormatCount
            return icd_term->dispatch.GetPhysicalDeviceSurfaceFormatsKHR(phys_dev_term->phys_dev, surface, pSurfaceFormatCount,
                                                                         NULL);
        } else {
            // Allocate a temporary array for the output of the old function
            VkSurfaceFormatKHR *formats = loader_stack_alloc(*pSurfaceFormatCount * sizeof(VkSurfaceFormatKHR));
            if (formats == NULL) {
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }

            VkResult res = icd_term->dispatch.GetPhysicalDeviceSurfaceFormatsKHR(phys_dev_term->phys_dev, surface,
                                                                                 pSurfaceFormatCount, formats);
            for (uint32_t i = 0; i < *pSurfaceFormatCount; ++i) {
                pSurfaceFormats[i].surfaceFormat = formats[i];
                if (pSurfaceFormats[i].pNext != NULL) {
                    loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                               "vkGetPhysicalDeviceSurfaceFormats2KHR: Emulation found unrecognized structure type in "
                               "pSurfaceFormats[%d].pNext - this struct will be ignored",
                               i);
                }
            }
            return res;
        }
    }
}

// ---- VK_EXT_display_surface_counter extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDeviceSurfaceCapabilities2EXT(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                                                        VkSurfaceCapabilities2EXT *pSurfaceCapabilities) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    return disp->GetPhysicalDeviceSurfaceCapabilities2EXT(unwrapped_phys_dev, surface, pSurfaceCapabilities);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_GetPhysicalDeviceSurfaceCapabilities2EXT(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilities2EXT *pSurfaceCapabilities) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    VkIcdSurface *icd_surface = (VkIcdSurface *)(surface);
    uint8_t icd_index = phys_dev_term->icd_index;

    // Unwrap the surface if needed
    VkSurfaceKHR unwrapped_surface = surface;
    if (icd_surface->real_icd_surfaces != NULL && (void *)icd_surface->real_icd_surfaces[icd_index] != NULL) {
        unwrapped_surface = icd_surface->real_icd_surfaces[icd_index];
    }

    if (icd_term->dispatch.GetPhysicalDeviceSurfaceCapabilities2EXT != NULL) {
        // Pass the call to the driver
        return icd_term->dispatch.GetPhysicalDeviceSurfaceCapabilities2EXT(phys_dev_term->phys_dev, unwrapped_surface,
                                                                           pSurfaceCapabilities);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceSurfaceCapabilities2EXT: Emulating call in ICD \"%s\" using "
                   "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
                   icd_term->scanned_icd->lib_name);

        VkSurfaceCapabilitiesKHR surface_caps;
        VkResult res =
            icd_term->dispatch.GetPhysicalDeviceSurfaceCapabilitiesKHR(phys_dev_term->phys_dev, unwrapped_surface, &surface_caps);
        pSurfaceCapabilities->minImageCount = surface_caps.minImageCount;
        pSurfaceCapabilities->maxImageCount = surface_caps.maxImageCount;
        pSurfaceCapabilities->currentExtent = surface_caps.currentExtent;
        pSurfaceCapabilities->minImageExtent = surface_caps.minImageExtent;
        pSurfaceCapabilities->maxImageExtent = surface_caps.maxImageExtent;
        pSurfaceCapabilities->maxImageArrayLayers = surface_caps.maxImageArrayLayers;
        pSurfaceCapabilities->supportedTransforms = surface_caps.supportedTransforms;
        pSurfaceCapabilities->currentTransform = surface_caps.currentTransform;
        pSurfaceCapabilities->supportedCompositeAlpha = surface_caps.supportedCompositeAlpha;
        pSurfaceCapabilities->supportedUsageFlags = surface_caps.supportedUsageFlags;
        pSurfaceCapabilities->supportedSurfaceCounters = 0;

        if (pSurfaceCapabilities->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceSurfaceCapabilities2EXT: Emulation found unrecognized structure type in "
                       "pSurfaceCapabilities->pNext - this struct will be ignored");
        }

        return res;
    }
}

// ---- VK_EXT_direct_mode_display extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL ReleaseDisplayEXT(VkPhysicalDevice physicalDevice, VkDisplayKHR display) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    return disp->ReleaseDisplayEXT(unwrapped_phys_dev, display);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_ReleaseDisplayEXT(VkPhysicalDevice physicalDevice, VkDisplayKHR display) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    if (icd_term->dispatch.ReleaseDisplayEXT == NULL) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "ICD \"%s\" associated with VkPhysicalDevice does not support vkReleaseDisplayEXT - Consequently, the call is "
                   "invalid because it should not be possible to acquire a display on this device",
                   icd_term->scanned_icd->lib_name);
    }
    return icd_term->dispatch.ReleaseDisplayEXT(phys_dev_term->phys_dev, display);
}

// ---- VK_EXT_acquire_xlib_display extension trampoline/terminators

#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
VKAPI_ATTR VkResult VKAPI_CALL AcquireXlibDisplayEXT(VkPhysicalDevice physicalDevice, Display *dpy, VkDisplayKHR display) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    return disp->AcquireXlibDisplayEXT(unwrapped_phys_dev, dpy, display);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_AcquireXlibDisplayEXT(VkPhysicalDevice physicalDevice, Display *dpy,
                                                                VkDisplayKHR display) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    if (icd_term->dispatch.AcquireXlibDisplayEXT != NULL) {
        // Pass the call to the driver
        return icd_term->dispatch.AcquireXlibDisplayEXT(phys_dev_term->phys_dev, dpy, display);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkAcquireXLibDisplayEXT: Emulating call in ICD \"%s\" by returning error", icd_term->scanned_icd->lib_name);

        // Fail for the unsupported command
        return VK_ERROR_INITIALIZATION_FAILED;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL GetRandROutputDisplayEXT(VkPhysicalDevice physicalDevice, Display *dpy, RROutput rrOutput,
                                                        VkDisplayKHR *pDisplay) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    return disp->GetRandROutputDisplayEXT(unwrapped_phys_dev, dpy, rrOutput, pDisplay);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_GetRandROutputDisplayEXT(VkPhysicalDevice physicalDevice, Display *dpy, RROutput rrOutput,
                                                                   VkDisplayKHR *pDisplay) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    if (icd_term->dispatch.GetRandROutputDisplayEXT != NULL) {
        // Pass the call to the driver
        return icd_term->dispatch.GetRandROutputDisplayEXT(phys_dev_term->phys_dev, dpy, rrOutput, pDisplay);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetRandROutputDisplayEXT: Emulating call in ICD \"%s\" by returning null display",
                   icd_term->scanned_icd->lib_name);

        // Return a null handle to indicate this can't be done
        *pDisplay = VK_NULL_HANDLE;
        return VK_SUCCESS;
    }
}

#endif  // VK_USE_PLATFORM_XLIB_XRANDR_EXT

// ---- VK_KHR_external_memory_capabilities extension trampoline/terminators

VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceExternalBufferPropertiesKHR(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfoKHR *pExternalBufferInfo,
    VkExternalBufferPropertiesKHR *pExternalBufferProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceExternalBufferPropertiesKHR(unwrapped_phys_dev, pExternalBufferInfo, pExternalBufferProperties);
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceExternalBufferPropertiesKHR(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfoKHR *pExternalBufferInfo,
    VkExternalBufferPropertiesKHR *pExternalBufferProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    if (icd_term->dispatch.GetPhysicalDeviceExternalBufferPropertiesKHR) {
        // Pass the call to the driver
        icd_term->dispatch.GetPhysicalDeviceExternalBufferPropertiesKHR(phys_dev_term->phys_dev, pExternalBufferInfo,
                                                                        pExternalBufferProperties);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceExternalBufferPropertiesKHR: Emulating call in ICD \"%s\"", icd_term->scanned_icd->lib_name);

        if (pExternalBufferInfo->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceExternalBufferPropertiesKHR: Emulation found unrecognized structure type in "
                       "pExternalBufferInfo->pNext - this struct will be ignored");
        }

        // Fill in everything being unsupported
        memset(&pExternalBufferProperties->externalMemoryProperties, 0, sizeof(VkExternalMemoryPropertiesKHR));

        if (pExternalBufferProperties->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceExternalBufferPropertiesKHR: Emulation found unrecognized structure type in "
                       "pExternalBufferProperties->pNext - this struct will be ignored");
        }
    }
}

// ---- VK_KHR_external_semaphore_capabilities extension trampoline/terminators

VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceExternalSemaphorePropertiesKHR(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfoKHR *pExternalSemaphoreInfo,
    VkExternalSemaphorePropertiesKHR *pExternalSemaphoreProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceExternalSemaphorePropertiesKHR(unwrapped_phys_dev, pExternalSemaphoreInfo, pExternalSemaphoreProperties);
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceExternalSemaphorePropertiesKHR(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalSemaphoreInfoKHR *pExternalSemaphoreInfo,
    VkExternalSemaphorePropertiesKHR *pExternalSemaphoreProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    if (icd_term->dispatch.GetPhysicalDeviceExternalSemaphorePropertiesKHR != NULL) {
        // Pass the call to the driver
        icd_term->dispatch.GetPhysicalDeviceExternalSemaphorePropertiesKHR(phys_dev_term->phys_dev, pExternalSemaphoreInfo,
                                                                           pExternalSemaphoreProperties);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR: Emulating call in ICD \"%s\"",
                   icd_term->scanned_icd->lib_name);

        if (pExternalSemaphoreInfo->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR: Emulation found unrecognized structure type in "
                       "pExternalSemaphoreInfo->pNext - this struct will be ignored");
        }

        // Fill in everything being unsupported
        pExternalSemaphoreProperties->exportFromImportedHandleTypes = 0;
        pExternalSemaphoreProperties->compatibleHandleTypes = 0;
        pExternalSemaphoreProperties->externalSemaphoreFeatures = 0;

        if (pExternalSemaphoreProperties->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR: Emulation found unrecognized structure type in "
                       "pExternalSemaphoreProperties->pNext - this struct will be ignored");
        }
    }
}

// ---- VK_KHR_external_fence_capabilities extension trampoline/terminators

VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceExternalFencePropertiesKHR(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfoKHR *pExternalFenceInfo,
    VkExternalFencePropertiesKHR *pExternalFenceProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceExternalFencePropertiesKHR(unwrapped_phys_dev, pExternalFenceInfo, pExternalFenceProperties);
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceExternalFencePropertiesKHR(
    VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalFenceInfoKHR *pExternalFenceInfo,
    VkExternalFencePropertiesKHR *pExternalFenceProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;

    if (icd_term->dispatch.GetPhysicalDeviceExternalFencePropertiesKHR != NULL) {
        // Pass the call to the driver
        icd_term->dispatch.GetPhysicalDeviceExternalFencePropertiesKHR(phys_dev_term->phys_dev, pExternalFenceInfo,
                                                                       pExternalFenceProperties);
    } else {
        // Emulate the call
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_INFORMATION_BIT_EXT, 0,
                   "vkGetPhysicalDeviceExternalFencePropertiesKHR: Emulating call in ICD \"%s\"", icd_term->scanned_icd->lib_name);

        if (pExternalFenceInfo->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceExternalFencePropertiesKHR: Emulation found unrecognized structure type in "
                       "pExternalFenceInfo->pNext - this struct will be ignored");
        }

        // Fill in everything being unsupported
        pExternalFenceProperties->exportFromImportedHandleTypes = 0;
        pExternalFenceProperties->compatibleHandleTypes = 0;
        pExternalFenceProperties->externalFenceFeatures = 0;

        if (pExternalFenceProperties->pNext != NULL) {
            loader_log(icd_term->this_instance, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0,
                       "vkGetPhysicalDeviceExternalFencePropertiesKHR: Emulation found unrecognized structure type in "
                       "pExternalFenceProperties->pNext - this struct will be ignored");
        }
    }
}

// ---- Helper functions

VkResult setupLoaderTrampPhysDevGroups(VkInstance instance) {
    VkResult res = VK_SUCCESS;
    struct loader_instance *inst;
    uint32_t total_count = 0;
    VkPhysicalDeviceGroupPropertiesKHX **new_phys_dev_groups = NULL;
    VkPhysicalDeviceGroupPropertiesKHX *local_phys_dev_groups = NULL;

    inst = loader_get_instance(instance);
    if (NULL == inst) {
        res = VK_ERROR_INITIALIZATION_FAILED;
        goto out;
    }

    // Setup the trampoline loader physical devices.  This will actually
    // call down and setup the terminator loader physical devices during the
    // process.
    VkResult setup_res = setupLoaderTrampPhysDevs(instance);
    if (setup_res != VK_SUCCESS && setup_res != VK_INCOMPLETE) {
        res = setup_res;
        goto out;
    }

    // Query how many physical device groups there
    res = inst->disp->layer_inst_disp.EnumeratePhysicalDeviceGroupsKHX(instance, &total_count, NULL);
    if (res != VK_SUCCESS) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "setupLoaderTrampPhysDevGroups:  Failed during dispatch call of "
                   "\'EnumeratePhysicalDeviceGroupsKHX\' to lower layers or "
                   "loader to get count.");
        goto out;
    }

    // Create an array for the new physical device groups, which will be stored
    // in the instance for the trampoline code.
    new_phys_dev_groups = (VkPhysicalDeviceGroupPropertiesKHX **)loader_instance_heap_alloc(
        inst, total_count * sizeof(VkPhysicalDeviceGroupPropertiesKHX *), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (NULL == new_phys_dev_groups) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "setupLoaderTrampPhysDevGroups:  Failed to allocate new physical device"
                   " group array of size %d",
                   total_count);
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }
    memset(new_phys_dev_groups, 0, total_count * sizeof(VkPhysicalDeviceGroupPropertiesKHX *));

    // Create a temporary array (on the stack) to keep track of the
    // returned VkPhysicalDevice values.
    local_phys_dev_groups = loader_stack_alloc(sizeof(VkPhysicalDeviceGroupPropertiesKHX) * total_count);
    if (NULL == local_phys_dev_groups) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "setupLoaderTrampPhysDevGroups:  Failed to allocate local "
                   "physical device group array of size %d",
                   total_count);
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }
    // Initialize the memory to something valid
    memset(local_phys_dev_groups, 0, sizeof(VkPhysicalDeviceGroupPropertiesKHX) * total_count);
    for (uint32_t group = 0; group < total_count; group++) {
        local_phys_dev_groups[group].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES_KHX;
        local_phys_dev_groups[group].pNext = NULL;
        local_phys_dev_groups[group].subsetAllocation = false;
    }

    // Call down and get the content
    res = inst->disp->layer_inst_disp.EnumeratePhysicalDeviceGroupsKHX(instance, &total_count, local_phys_dev_groups);
    if (VK_SUCCESS != res) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "setupLoaderTrampPhysDevGroups:  Failed during dispatch call of "
                   "\'EnumeratePhysicalDeviceGroupsKHX\' to lower layers or "
                   "loader to get content.");
        goto out;
    }

    // Replace all the physical device IDs with the proper loader values
    for (uint32_t group = 0; group < total_count; group++) {
        for (uint32_t group_gpu = 0; group_gpu < local_phys_dev_groups[group].physicalDeviceCount; group_gpu++) {
            bool found = false;
            for (uint32_t tramp_gpu = 0; tramp_gpu < inst->phys_dev_count_tramp; tramp_gpu++) {
                if (local_phys_dev_groups[group].physicalDevices[group_gpu] == inst->phys_devs_tramp[tramp_gpu]->phys_dev) {
                    local_phys_dev_groups[group].physicalDevices[group_gpu] = (VkPhysicalDevice)inst->phys_devs_tramp[tramp_gpu];
                    found = true;
                    break;
                }
            }
            if (!found) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "setupLoaderTrampPhysDevGroups:  Failed to find GPU %d in group %d"
                           " returned by \'EnumeratePhysicalDeviceGroupsKHX\' in list returned"
                           " by \'EnumeratePhysicalDevices\'", group_gpu, group);
                res = VK_ERROR_INITIALIZATION_FAILED;
                goto out;
            }
        }
    }

    // Copy or create everything to fill the new array of physical device groups
    for (uint32_t new_idx = 0; new_idx < total_count; new_idx++) {
        // Check if this physical device group with the same contents is already in the old buffer
        for (uint32_t old_idx = 0; old_idx < inst->phys_dev_group_count_tramp; old_idx++) {
            if (local_phys_dev_groups[new_idx].physicalDeviceCount == inst->phys_dev_groups_tramp[old_idx]->physicalDeviceCount) {
                bool found_all_gpus = true;
                for (uint32_t old_gpu = 0; old_gpu < inst->phys_dev_groups_tramp[old_idx]->physicalDeviceCount; old_gpu++) {
                    bool found_gpu = false;
                    for (uint32_t new_gpu = 0; new_gpu < local_phys_dev_groups[new_idx].physicalDeviceCount; new_gpu++) {
                        if (local_phys_dev_groups[new_idx].physicalDevices[new_gpu] == inst->phys_dev_groups_tramp[old_idx]->physicalDevices[old_gpu]) {
                            found_gpu = true;
                            break;
                        }
                    }

                    if (!found_gpu) {
                        found_all_gpus = false;
                        break;
                    }
                }
                if (!found_all_gpus) {
                    continue;
                } else {
                    new_phys_dev_groups[new_idx] = inst->phys_dev_groups_tramp[old_idx];
                    break;
                }
            }
        }

        // If this physical device group isn't in the old buffer, create it
        if (NULL == new_phys_dev_groups[new_idx]) {
            new_phys_dev_groups[new_idx] = (VkPhysicalDeviceGroupPropertiesKHX *)loader_instance_heap_alloc(
                inst, sizeof(VkPhysicalDeviceGroupPropertiesKHX), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
            if (NULL == new_phys_dev_groups[new_idx]) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "setupLoaderTrampPhysDevGroups:  Failed to allocate "
                           "physical device group trampoline object %d",
                           new_idx);
                total_count = new_idx;
                res = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto out;
            }
            memcpy(new_phys_dev_groups[new_idx], &local_phys_dev_groups[new_idx],
                   sizeof(VkPhysicalDeviceGroupPropertiesKHX));
        }
    }

out:

    if (VK_SUCCESS != res) {
        if (NULL != new_phys_dev_groups) {
            for (uint32_t i = 0; i < total_count; i++) {
                loader_instance_heap_free(inst, new_phys_dev_groups[i]);
            }
            loader_instance_heap_free(inst, new_phys_dev_groups);
        }
        total_count = 0;
    } else {
        // Free everything that didn't carry over to the new array of
        // physical device groups
        if (NULL != inst->phys_dev_groups_tramp) {
            for (uint32_t i = 0; i < inst->phys_dev_group_count_tramp; i++) {
                bool found = false;
                for (uint32_t j = 0; j < total_count; j++) {
                    if (inst->phys_dev_groups_tramp[i] == new_phys_dev_groups[j]) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    loader_instance_heap_free(inst, inst->phys_dev_groups_tramp[i]);
                }
            }
            loader_instance_heap_free(inst, inst->phys_dev_groups_tramp);
        }

        // Swap in the new physical device group list
        inst->phys_dev_group_count_tramp = total_count;
        inst->phys_dev_groups_tramp = new_phys_dev_groups;
    }

    return res;
}

VkResult setupLoaderTermPhysDevGroups(struct loader_instance *inst) {
    VkResult res = VK_SUCCESS;
    struct loader_icd_term *icd_term;
    uint32_t total_count = 0;
    uint32_t cur_icd_group_count = 0;
    VkPhysicalDeviceGroupPropertiesKHX **new_phys_dev_groups = NULL;
    VkPhysicalDeviceGroupPropertiesKHX *local_phys_dev_groups = NULL;

    if (0 == inst->phys_dev_count_term) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "setupLoaderTermPhysDevGroups:  Loader failed to setup physical "
                   "device terminator info before calling \'EnumeratePhysicalDeviceGroupsKHX\'.");
        assert(false);
        res = VK_ERROR_INITIALIZATION_FAILED;
        goto out;
    }

    // For each ICD, query the number of physical device groups, and then get an
    // internal value for those physical devices.
    icd_term = inst->icd_terms;
    for (uint32_t icd_idx = 0; NULL != icd_term; icd_term = icd_term->next, icd_idx++) {
        cur_icd_group_count = 0;
        if (NULL == icd_term->dispatch.EnumeratePhysicalDeviceGroupsKHX) {
            // Treat each ICD's GPU as it's own group if the extension isn't supported
            res = icd_term->dispatch.EnumeratePhysicalDevices(icd_term->instance, &cur_icd_group_count, NULL);
            if (res != VK_SUCCESS) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "setupLoaderTermPhysDevGroups:  Failed during dispatch call of "
                           "\'EnumeratePhysicalDevices\' to ICD %d to get plain phys dev count.",
                           icd_idx);
                goto out;
            }
        } else {
            // Query the actual group info
            res = icd_term->dispatch.EnumeratePhysicalDeviceGroupsKHX(icd_term->instance, &cur_icd_group_count, NULL);
            if (res != VK_SUCCESS) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "setupLoaderTermPhysDevGroups:  Failed during dispatch call of "
                           "\'EnumeratePhysicalDeviceGroupsKHX\' to ICD %d to get count.",
                           icd_idx);
                goto out;
            }
        }
        total_count += cur_icd_group_count;
    }

    // Create an array for the new physical device groups, which will be stored
    // in the instance for the Terminator code.
    new_phys_dev_groups = (VkPhysicalDeviceGroupPropertiesKHX **)loader_instance_heap_alloc(
        inst, total_count * sizeof(VkPhysicalDeviceGroupPropertiesKHX *), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (NULL == new_phys_dev_groups) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "setupLoaderTermPhysDevGroups:  Failed to allocate new physical device"
                   " group array of size %d",
                   total_count);
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }
    memset(new_phys_dev_groups, 0, total_count * sizeof(VkPhysicalDeviceGroupPropertiesKHX *));

    // Create a temporary array (on the stack) to keep track of the
    // returned VkPhysicalDevice values.
    local_phys_dev_groups = loader_stack_alloc(sizeof(VkPhysicalDeviceGroupPropertiesKHX) * total_count);
    if (NULL == local_phys_dev_groups) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "setupLoaderTermPhysDevGroups:  Failed to allocate local "
                   "physical device group array of size %d",
                   total_count);
        res = VK_ERROR_OUT_OF_HOST_MEMORY;
        goto out;
    }
    // Initialize the memory to something valid
    memset(local_phys_dev_groups, 0, sizeof(VkPhysicalDeviceGroupPropertiesKHX) * total_count);
    for (uint32_t group = 0; group < total_count; group++) {
        local_phys_dev_groups[group].sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES_KHX;
        local_phys_dev_groups[group].pNext = NULL;
        local_phys_dev_groups[group].subsetAllocation = false;
    }

    cur_icd_group_count = 0;
    icd_term = inst->icd_terms;
    for (uint32_t icd_idx = 0; NULL != icd_term; icd_term = icd_term->next, icd_idx++) {
        uint32_t count_this_time = total_count - cur_icd_group_count;

        if (NULL == icd_term->dispatch.EnumeratePhysicalDeviceGroupsKHX) {
            VkPhysicalDevice* phys_dev_array = loader_stack_alloc(sizeof(VkPhysicalDevice) * count_this_time);
            if (NULL == phys_dev_array) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "setupLoaderTermPhysDevGroups:  Failed to allocate local "
                           "physical device array of size %d",
                           count_this_time);
                res = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto out;
            }

            res = icd_term->dispatch.EnumeratePhysicalDevices(icd_term->instance, &count_this_time, phys_dev_array);
            if (res != VK_SUCCESS) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "setupLoaderTermPhysDevGroups:  Failed during dispatch call of "
                           "\'EnumeratePhysicalDevices\' to ICD %d to get plain phys dev count.",
                           icd_idx);
                goto out;
            }

            // Add each GPU as it's own group
            for (uint32_t indiv_gpu = 0; indiv_gpu < count_this_time; indiv_gpu++) {
                local_phys_dev_groups[indiv_gpu + cur_icd_group_count].physicalDeviceCount = 1;
                local_phys_dev_groups[indiv_gpu + cur_icd_group_count].physicalDevices[0] = phys_dev_array[indiv_gpu];
            }

        } else {
            res = icd_term->dispatch.EnumeratePhysicalDeviceGroupsKHX(icd_term->instance, &count_this_time, &local_phys_dev_groups[cur_icd_group_count]);
            if (VK_SUCCESS != res) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "setupLoaderTermPhysDevGroups:  Failed during dispatch call of "
                           "\'EnumeratePhysicalDeviceGroupsKHX\' to ICD %d to get content.",
                           icd_idx);
                goto out;
            }
        }

        cur_icd_group_count += count_this_time;
    }

    // Replace all the physical device IDs with the proper loader values
    for (uint32_t group = 0; group < total_count; group++) {
        for (uint32_t group_gpu = 0; group_gpu < local_phys_dev_groups[group].physicalDeviceCount; group_gpu++) {
            bool found = false;
            for (uint32_t term_gpu = 0; term_gpu < inst->phys_dev_count_term; term_gpu++) {
                if (local_phys_dev_groups[group].physicalDevices[group_gpu] == inst->phys_devs_term[term_gpu]->phys_dev) {
                    local_phys_dev_groups[group].physicalDevices[group_gpu] = (VkPhysicalDevice)inst->phys_devs_term[term_gpu];
                    found = true;
                    break;
                }
            }
            if (!found) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "setupLoaderTermPhysDevGroups:  Failed to find GPU %d in group %d"
                           " returned by \'EnumeratePhysicalDeviceGroupsKHX\' in list returned"
                           " by \'EnumeratePhysicalDevices\'", group_gpu, group);
                res = VK_ERROR_INITIALIZATION_FAILED;
                goto out;
            }
        }
    }

    // Copy or create everything to fill the new array of physical device groups
    for (uint32_t new_idx = 0; new_idx < total_count; new_idx++) {
        // Check if this physical device group with the same contents is already in the old buffer
        for (uint32_t old_idx = 0; old_idx < inst->phys_dev_group_count_term; old_idx++) {
            if (local_phys_dev_groups[new_idx].physicalDeviceCount == inst->phys_dev_groups_term[old_idx]->physicalDeviceCount) {
                bool found_all_gpus = true;
                for (uint32_t old_gpu = 0; old_gpu < inst->phys_dev_groups_term[old_idx]->physicalDeviceCount; old_gpu++) {
                    bool found_gpu = false;
                    for (uint32_t new_gpu = 0; new_gpu < local_phys_dev_groups[new_idx].physicalDeviceCount; new_gpu++) {
                        if (local_phys_dev_groups[new_idx].physicalDevices[new_gpu] == inst->phys_dev_groups_term[old_idx]->physicalDevices[old_gpu]) {
                            found_gpu = true;
                            break;
                        }
                    }

                    if (!found_gpu) {
                        found_all_gpus = false;
                        break;
                    }
                }
                if (!found_all_gpus) {
                    continue;
                } else {
                    new_phys_dev_groups[new_idx] = inst->phys_dev_groups_term[old_idx];
                    break;
                }
            }
        }

        // If this physical device group isn't in the old buffer, create it
        if (NULL == new_phys_dev_groups[new_idx]) {
            new_phys_dev_groups[new_idx] = (VkPhysicalDeviceGroupPropertiesKHX *)loader_instance_heap_alloc(
                inst, sizeof(VkPhysicalDeviceGroupPropertiesKHX), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
            if (NULL == new_phys_dev_groups[new_idx]) {
                loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                           "setupLoaderTermPhysDevGroups:  Failed to allocate "
                           "physical device group Terminator object %d",
                           new_idx);
                total_count = new_idx;
                res = VK_ERROR_OUT_OF_HOST_MEMORY;
                goto out;
            }
            memcpy(new_phys_dev_groups[new_idx], &local_phys_dev_groups[new_idx],
                   sizeof(VkPhysicalDeviceGroupPropertiesKHX));
        }
    }

out:

    if (VK_SUCCESS != res) {
        if (NULL != new_phys_dev_groups) {
            for (uint32_t i = 0; i < total_count; i++) {
                loader_instance_heap_free(inst, new_phys_dev_groups[i]);
            }
            loader_instance_heap_free(inst, new_phys_dev_groups);
        }
        total_count = 0;
    } else {
        // Free everything that didn't carry over to the new array of
        // physical device groups
        if (NULL != inst->phys_dev_groups_term) {
            for (uint32_t i = 0; i < inst->phys_dev_group_count_term; i++) {
                bool found = false;
                for (uint32_t j = 0; j < total_count; j++) {
                    if (inst->phys_dev_groups_term[i] == new_phys_dev_groups[j]) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    loader_instance_heap_free(inst, inst->phys_dev_groups_term[i]);
                }
            }
            loader_instance_heap_free(inst, inst->phys_dev_groups_term);
        }

        // Swap in the new physical device group list
        inst->phys_dev_group_count_term = total_count;
        inst->phys_dev_groups_term = new_phys_dev_groups;
    }

    return res;
}
