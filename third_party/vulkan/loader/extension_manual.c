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