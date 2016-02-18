/*
 *
 * Copyright (c) 2015-2016 The Khronos Group Inc.
 * Copyright (c) 2015-2016 Valve Corporation
 * Copyright (c) 2015-2016 LunarG, Inc.
 * Copyright (C) 2015 Google Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and/or associated documentation files (the "Materials"), to
 * deal in the Materials without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Materials, and to permit persons to whom the Materials are
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice(s) and this permission notice shall be included in
 * all copies or substantial portions of the Materials.
 *
 * THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE MATERIALS OR THE
 * USE OR OTHER DEALINGS IN THE MATERIALS.
 *
 * Author: Courtney Goeltzenleuchter <courtney@lunarg.com>
 * Author: Jon Ashburn <jon@lunarg.com>
 * Author: Tony Barbour <tony@LunarG.com>
 * Author: Chia-I Wu <olv@lunarg.com>
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>

#include "vk_loader_platform.h"
#include "loader.h"
#include "debug_report.h"
#include "wsi.h"

/* Trampoline entrypoints */
LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator,
                 VkInstance *pInstance) {
    struct loader_instance *ptr_instance = NULL;
    VkInstance created_instance = VK_NULL_HANDLE;
    VkResult res = VK_ERROR_INITIALIZATION_FAILED;
    VkDebugReportCallbackEXT instance_callback = VK_NULL_HANDLE;
    void *pNext = (void *)pCreateInfo->pNext;

    loader_platform_thread_once(&once_init, loader_initialize);

#if 0
	if (pAllocator) {
        ptr_instance = (struct loader_instance *) pAllocator->pfnAllocation(
                           pAllocator->pUserData,
                           sizeof(struct loader_instance),
                           sizeof(int *),
                           VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    } else {
#endif
    ptr_instance =
        (struct loader_instance *)malloc(sizeof(struct loader_instance));
    //}
    if (ptr_instance == NULL) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    tls_instance = ptr_instance;
    loader_platform_thread_lock_mutex(&loader_lock);
    memset(ptr_instance, 0, sizeof(struct loader_instance));
#if 0
    if (pAllocator) {
        ptr_instance->alloc_callbacks = *pAllocator;
    }
#endif

    /*
     * Look for a debug report create info structure
     * and setup a callback if found.
     */
    while (pNext) {
        if (((VkInstanceCreateInfo *)pNext)->sType ==
            VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT) {
            instance_callback = (VkDebugReportCallbackEXT)ptr_instance;
            if (util_CreateDebugReportCallback(ptr_instance, pNext, NULL,
                                               instance_callback)) {
                loader_heap_free(ptr_instance, ptr_instance);
                loader_platform_thread_unlock_mutex(&loader_lock);
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
        }
        pNext = (void *)((VkInstanceCreateInfo *)pNext)->pNext;
    }

    /* Due to implicit layers need to get layer list even if
     * enabledLayerCount == 0 and VK_INSTANCE_LAYERS is unset. For now always
     * get layer list (both instance and device) via loader_layer_scan(). */
    memset(&ptr_instance->instance_layer_list, 0,
           sizeof(ptr_instance->instance_layer_list));
    memset(&ptr_instance->device_layer_list, 0,
           sizeof(ptr_instance->device_layer_list));
    loader_layer_scan(ptr_instance, &ptr_instance->instance_layer_list,
                      &ptr_instance->device_layer_list);

    /* validate the app requested layers to be enabled */
    if (pCreateInfo->enabledLayerCount > 0) {
        res =
            loader_validate_layers(ptr_instance, pCreateInfo->enabledLayerCount,
                                   pCreateInfo->ppEnabledLayerNames,
                                   &ptr_instance->instance_layer_list);
        if (res != VK_SUCCESS) {
            util_DestroyDebugReportCallback(ptr_instance, instance_callback,
                                            NULL);
            loader_heap_free(ptr_instance, ptr_instance);
            loader_platform_thread_unlock_mutex(&loader_lock);
            return res;
        }
    }

    /* convert any meta layers to the actual layers makes a copy of layer name*/
    uint32_t saved_layer_count = pCreateInfo->enabledLayerCount;
    char **saved_layer_names;
    char **saved_layer_ptr;
    saved_layer_names =
        loader_stack_alloc(sizeof(char *) * pCreateInfo->enabledLayerCount);
    for (uint32_t i = 0; i < saved_layer_count; i++) {
        saved_layer_names[i] = (char *)pCreateInfo->ppEnabledLayerNames[i];
    }
    saved_layer_ptr = (char **)pCreateInfo->ppEnabledLayerNames;

    loader_expand_layer_names(
        ptr_instance, std_validation_str,
        sizeof(std_validation_names) / sizeof(std_validation_names[0]),
        std_validation_names, (uint32_t *)&pCreateInfo->enabledLayerCount,
        (char ***)&pCreateInfo->ppEnabledLayerNames);

    /* Scan/discover all ICD libraries */
    memset(&ptr_instance->icd_libs, 0, sizeof(ptr_instance->icd_libs));
    loader_icd_scan(ptr_instance, &ptr_instance->icd_libs);

    /* get extensions from all ICD's, merge so no duplicates, then validate */
    loader_get_icd_loader_instance_extensions(
        ptr_instance, &ptr_instance->icd_libs, &ptr_instance->ext_list);
    res = loader_validate_instance_extensions(
        ptr_instance, &ptr_instance->ext_list,
        &ptr_instance->instance_layer_list, pCreateInfo);
    if (res != VK_SUCCESS) {
        loader_unexpand_inst_layer_names(ptr_instance, saved_layer_count,
                                         saved_layer_names, saved_layer_ptr,
                                         pCreateInfo);
        loader_delete_layer_properties(ptr_instance,
                                       &ptr_instance->device_layer_list);
        loader_delete_layer_properties(ptr_instance,
                                       &ptr_instance->instance_layer_list);
        loader_scanned_icd_clear(ptr_instance, &ptr_instance->icd_libs);
        loader_destroy_generic_list(
            ptr_instance,
            (struct loader_generic_list *)&ptr_instance->ext_list);
        util_DestroyDebugReportCallback(ptr_instance, instance_callback, NULL);
        loader_platform_thread_unlock_mutex(&loader_lock);
        loader_heap_free(ptr_instance, ptr_instance);
        return res;
    }

    ptr_instance->disp =
        loader_heap_alloc(ptr_instance, sizeof(VkLayerInstanceDispatchTable),
                          VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (ptr_instance->disp == NULL) {
        loader_unexpand_inst_layer_names(ptr_instance, saved_layer_count,
                                         saved_layer_names, saved_layer_ptr,
                                         pCreateInfo);
        loader_delete_layer_properties(ptr_instance,
                                       &ptr_instance->device_layer_list);
        loader_delete_layer_properties(ptr_instance,
                                       &ptr_instance->instance_layer_list);
        loader_scanned_icd_clear(ptr_instance, &ptr_instance->icd_libs);
        loader_destroy_generic_list(
            ptr_instance,
            (struct loader_generic_list *)&ptr_instance->ext_list);
        util_DestroyDebugReportCallback(ptr_instance, instance_callback, NULL);
        loader_platform_thread_unlock_mutex(&loader_lock);
        loader_heap_free(ptr_instance, ptr_instance);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memcpy(ptr_instance->disp, &instance_disp, sizeof(instance_disp));
    ptr_instance->next = loader.instances;
    loader.instances = ptr_instance;

    /* activate any layers on instance chain */
    res = loader_enable_instance_layers(ptr_instance, pCreateInfo,
                                        &ptr_instance->instance_layer_list);
    if (res != VK_SUCCESS) {
        loader_unexpand_inst_layer_names(ptr_instance, saved_layer_count,
                                         saved_layer_names, saved_layer_ptr,
                                         pCreateInfo);
        loader_delete_layer_properties(ptr_instance,
                                       &ptr_instance->device_layer_list);
        loader_delete_layer_properties(ptr_instance,
                                       &ptr_instance->instance_layer_list);
        loader_scanned_icd_clear(ptr_instance, &ptr_instance->icd_libs);
        loader_destroy_generic_list(
            ptr_instance,
            (struct loader_generic_list *)&ptr_instance->ext_list);
        loader.instances = ptr_instance->next;
        util_DestroyDebugReportCallback(ptr_instance, instance_callback, NULL);
        loader_platform_thread_unlock_mutex(&loader_lock);
        loader_heap_free(ptr_instance, ptr_instance->disp);
        loader_heap_free(ptr_instance, ptr_instance);
        return res;
    }

    created_instance = (VkInstance)ptr_instance;
    res = loader_create_instance_chain(pCreateInfo, pAllocator, ptr_instance,
                                       &created_instance);

    if (res == VK_SUCCESS) {
        wsi_create_instance(ptr_instance, pCreateInfo);
        debug_report_create_instance(ptr_instance, pCreateInfo);

        *pInstance = created_instance;

        /*
         * Finally have the layers in place and everyone has seen
         * the CreateInstance command go by. This allows the layer's
         * GetInstanceProcAddr functions to return valid extension functions
         * if enabled.
         */
        loader_activate_instance_layer_extensions(ptr_instance, *pInstance);
    } else {
        // TODO: cleanup here.
    }

    /* Remove temporary debug_report callback */
    util_DestroyDebugReportCallback(ptr_instance, instance_callback, NULL);
    loader_unexpand_inst_layer_names(ptr_instance, saved_layer_count,
                                     saved_layer_names, saved_layer_ptr,
                                     pCreateInfo);
    loader_platform_thread_unlock_mutex(&loader_lock);
    return res;
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyInstance(VkInstance instance,
                  const VkAllocationCallbacks *pAllocator) {
    const VkLayerInstanceDispatchTable *disp;
    struct loader_instance *ptr_instance = NULL;
    disp = loader_get_instance_dispatch(instance);

    loader_platform_thread_lock_mutex(&loader_lock);

    /* TODO: Do we need a temporary callback here to catch cleanup issues? */

    ptr_instance = loader_get_instance(instance);
    disp->DestroyInstance(instance, pAllocator);

    loader_deactivate_instance_layers(ptr_instance);
    loader_heap_free(ptr_instance, ptr_instance->disp);
    loader_heap_free(ptr_instance, ptr_instance);
    loader_platform_thread_unlock_mutex(&loader_lock);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumeratePhysicalDevices(VkInstance instance, uint32_t *pPhysicalDeviceCount,
                           VkPhysicalDevice *pPhysicalDevices) {
    const VkLayerInstanceDispatchTable *disp;
    VkResult res;
    disp = loader_get_instance_dispatch(instance);

    loader_platform_thread_lock_mutex(&loader_lock);
    res = disp->EnumeratePhysicalDevices(instance, pPhysicalDeviceCount,
                                         pPhysicalDevices);
    loader_platform_thread_unlock_mutex(&loader_lock);
    return res;
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFeatures(VkPhysicalDevice gpu,
                            VkPhysicalDeviceFeatures *pFeatures) {
    const VkLayerInstanceDispatchTable *disp;

    disp = loader_get_instance_dispatch(gpu);
    disp->GetPhysicalDeviceFeatures(gpu, pFeatures);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice gpu, VkFormat format,
                                    VkFormatProperties *pFormatInfo) {
    const VkLayerInstanceDispatchTable *disp;

    disp = loader_get_instance_dispatch(gpu);
    disp->GetPhysicalDeviceFormatProperties(gpu, format, pFormatInfo);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
    VkImageFormatProperties *pImageFormatProperties) {
    const VkLayerInstanceDispatchTable *disp;

    disp = loader_get_instance_dispatch(physicalDevice);
    return disp->GetPhysicalDeviceImageFormatProperties(
        physicalDevice, format, type, tiling, usage, flags,
        pImageFormatProperties);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceProperties(VkPhysicalDevice gpu,
                              VkPhysicalDeviceProperties *pProperties) {
    const VkLayerInstanceDispatchTable *disp;

    disp = loader_get_instance_dispatch(gpu);
    disp->GetPhysicalDeviceProperties(gpu, pProperties);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceQueueFamilyProperties(
    VkPhysicalDevice gpu, uint32_t *pQueueFamilyPropertyCount,
    VkQueueFamilyProperties *pQueueProperties) {
    const VkLayerInstanceDispatchTable *disp;

    disp = loader_get_instance_dispatch(gpu);
    disp->GetPhysicalDeviceQueueFamilyProperties(gpu, pQueueFamilyPropertyCount,
                                                 pQueueProperties);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice gpu, VkPhysicalDeviceMemoryProperties *pMemoryProperties) {
    const VkLayerInstanceDispatchTable *disp;

    disp = loader_get_instance_dispatch(gpu);
    disp->GetPhysicalDeviceMemoryProperties(gpu, pMemoryProperties);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDevice(VkPhysicalDevice gpu, const VkDeviceCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator, VkDevice *pDevice) {
    VkResult res;

    loader_platform_thread_lock_mutex(&loader_lock);

    res = loader_CreateDevice(gpu, pCreateInfo, pAllocator, pDevice);

    loader_platform_thread_unlock_mutex(&loader_lock);
    return res;
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyDevice(VkDevice device, const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;
    struct loader_device *dev;

    loader_platform_thread_lock_mutex(&loader_lock);

    struct loader_icd *icd = loader_get_icd_and_device(device, &dev);
    const struct loader_instance *inst = icd->this_instance;
    disp = loader_get_dispatch(device);

    disp->DestroyDevice(device, pAllocator);
    dev->device = NULL;
    loader_remove_logical_device(inst, icd, dev);

    loader_platform_thread_unlock_mutex(&loader_lock);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                     const char *pLayerName,
                                     uint32_t *pPropertyCount,
                                     VkExtensionProperties *pProperties) {
    VkResult res;

    loader_platform_thread_lock_mutex(&loader_lock);

    /* If pLayerName == NULL, then querying ICD extensions, pass this call
       down the instance chain which will terminate in the ICD. This allows
       layers to filter the extensions coming back up the chain.
       If pLayerName != NULL then get layer extensions from manifest file.  */
    if (pLayerName == NULL || strlen(pLayerName) == 0) {
        const VkLayerInstanceDispatchTable *disp;

        disp = loader_get_instance_dispatch(physicalDevice);
        res = disp->EnumerateDeviceExtensionProperties(
            physicalDevice, NULL, pPropertyCount, pProperties);
    } else {
        res = loader_EnumerateDeviceExtensionProperties(
            physicalDevice, pLayerName, pPropertyCount, pProperties);
    }

    loader_platform_thread_unlock_mutex(&loader_lock);
    return res;
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice,
                                 uint32_t *pPropertyCount,
                                 VkLayerProperties *pProperties) {
    VkResult res;

    loader_platform_thread_lock_mutex(&loader_lock);

    /* Don't dispatch this call down the instance chain, want all device layers
       enumerated and instance chain may not contain all device layers */
    res = loader_EnumerateDeviceLayerProperties(physicalDevice, pPropertyCount,
                                                pProperties);
    loader_platform_thread_unlock_mutex(&loader_lock);
    return res;
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetDeviceQueue(VkDevice device, uint32_t queueNodeIndex, uint32_t queueIndex,
                 VkQueue *pQueue) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->GetDeviceQueue(device, queueNodeIndex, queueIndex, pQueue);
    loader_set_dispatch(*pQueue, disp);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo *pSubmits,
              VkFence fence) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(queue);

    return disp->QueueSubmit(queue, submitCount, pSubmits, fence);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(queue);

    return disp->QueueWaitIdle(queue);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->DeviceWaitIdle(device);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pAllocateInfo,
                 const VkAllocationCallbacks *pAllocator,
                 VkDeviceMemory *pMemory) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->AllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkFreeMemory(VkDevice device, VkDeviceMemory mem,
             const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->FreeMemory(device, mem, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkMapMemory(VkDevice device, VkDeviceMemory mem, VkDeviceSize offset,
            VkDeviceSize size, VkFlags flags, void **ppData) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->MapMemory(device, mem, offset, size, flags, ppData);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkUnmapMemory(VkDevice device, VkDeviceMemory mem) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->UnmapMemory(device, mem);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkFlushMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                          const VkMappedMemoryRange *pMemoryRanges) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->FlushMappedMemoryRanges(device, memoryRangeCount,
                                         pMemoryRanges);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkInvalidateMappedMemoryRanges(VkDevice device, uint32_t memoryRangeCount,
                               const VkMappedMemoryRange *pMemoryRanges) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->InvalidateMappedMemoryRanges(device, memoryRangeCount,
                                              pMemoryRanges);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetDeviceMemoryCommitment(VkDevice device, VkDeviceMemory memory,
                            VkDeviceSize *pCommittedMemoryInBytes) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->GetDeviceMemoryCommitment(device, memory, pCommittedMemoryInBytes);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory mem,
                   VkDeviceSize offset) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->BindBufferMemory(device, buffer, mem, offset);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory mem,
                  VkDeviceSize offset) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->BindImageMemory(device, image, mem, offset);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer,
                              VkMemoryRequirements *pMemoryRequirements) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->GetBufferMemoryRequirements(device, buffer, pMemoryRequirements);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetImageMemoryRequirements(VkDevice device, VkImage image,
                             VkMemoryRequirements *pMemoryRequirements) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->GetImageMemoryRequirements(device, image, pMemoryRequirements);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL vkGetImageSparseMemoryRequirements(
    VkDevice device, VkImage image, uint32_t *pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements *pSparseMemoryRequirements) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->GetImageSparseMemoryRequirements(device, image,
                                           pSparseMemoryRequirementCount,
                                           pSparseMemoryRequirements);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkSampleCountFlagBits samples, VkImageUsageFlags usage,
    VkImageTiling tiling, uint32_t *pPropertyCount,
    VkSparseImageFormatProperties *pProperties) {
    const VkLayerInstanceDispatchTable *disp;

    disp = loader_get_instance_dispatch(physicalDevice);

    disp->GetPhysicalDeviceSparseImageFormatProperties(
        physicalDevice, format, type, samples, usage, tiling, pPropertyCount,
        pProperties);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkQueueBindSparse(VkQueue queue, uint32_t bindInfoCount,
                  const VkBindSparseInfo *pBindInfo, VkFence fence) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(queue);

    return disp->QueueBindSparse(queue, bindInfoCount, pBindInfo, fence);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFence(VkDevice device, const VkFenceCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkFence *pFence) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateFence(device, pCreateInfo, pAllocator, pFence);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyFence(VkDevice device, VkFence fence,
               const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroyFence(device, fence, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->ResetFences(device, fenceCount, pFences);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetFenceStatus(VkDevice device, VkFence fence) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->GetFenceStatus(device, fence);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkWaitForFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences,
                VkBool32 waitAll, uint64_t timeout) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->WaitForFences(device, fenceCount, pFences, waitAll, timeout);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkSemaphore *pSemaphore) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateSemaphore(device, pCreateInfo, pAllocator, pSemaphore);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroySemaphore(VkDevice device, VkSemaphore semaphore,
                   const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroySemaphore(device, semaphore, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateEvent(VkDevice device, const VkEventCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkEvent *pEvent) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateEvent(device, pCreateInfo, pAllocator, pEvent);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyEvent(VkDevice device, VkEvent event,
               const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroyEvent(device, event, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetEventStatus(VkDevice device, VkEvent event) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->GetEventStatus(device, event);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkSetEvent(VkDevice device, VkEvent event) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->SetEvent(device, event);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkResetEvent(VkDevice device, VkEvent event) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->ResetEvent(device, event);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkQueryPool *pQueryPool) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateQueryPool(device, pCreateInfo, pAllocator, pQueryPool);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool,
                   const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroyQueryPool(device, queryPool, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetQueryPoolResults(VkDevice device, VkQueryPool queryPool,
                      uint32_t firstQuery, uint32_t queryCount, size_t dataSize,
                      void *pData, VkDeviceSize stride,
                      VkQueryResultFlags flags) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->GetQueryPoolResults(device, queryPool, firstQuery, queryCount,
                                     dataSize, pData, stride, flags);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateBuffer(VkDevice device, const VkBufferCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator, VkBuffer *pBuffer) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateBuffer(device, pCreateInfo, pAllocator, pBuffer);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyBuffer(VkDevice device, VkBuffer buffer,
                const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroyBuffer(device, buffer, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateBufferView(VkDevice device, const VkBufferViewCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkBufferView *pView) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateBufferView(device, pCreateInfo, pAllocator, pView);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyBufferView(VkDevice device, VkBufferView bufferView,
                    const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroyBufferView(device, bufferView, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImage(VkDevice device, const VkImageCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator, VkImage *pImage) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateImage(device, pCreateInfo, pAllocator, pImage);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyImage(VkDevice device, VkImage image,
               const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroyImage(device, image, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetImageSubresourceLayout(VkDevice device, VkImage image,
                            const VkImageSubresource *pSubresource,
                            VkSubresourceLayout *pLayout) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->GetImageSubresourceLayout(device, image, pSubresource, pLayout);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator, VkImageView *pView) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateImageView(device, pCreateInfo, pAllocator, pView);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyImageView(VkDevice device, VkImageView imageView,
                   const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroyImageView(device, imageView, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateShaderModule(VkDevice device,
                     const VkShaderModuleCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkShaderModule *pShader) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateShaderModule(device, pCreateInfo, pAllocator, pShader);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule,
                      const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroyShaderModule(device, shaderModule, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreatePipelineCache(VkDevice device,
                      const VkPipelineCacheCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkPipelineCache *pPipelineCache) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreatePipelineCache(device, pCreateInfo, pAllocator,
                                     pPipelineCache);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyPipelineCache(VkDevice device, VkPipelineCache pipelineCache,
                       const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroyPipelineCache(device, pipelineCache, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPipelineCacheData(VkDevice device, VkPipelineCache pipelineCache,
                       size_t *pDataSize, void *pData) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->GetPipelineCacheData(device, pipelineCache, pDataSize, pData);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkMergePipelineCaches(VkDevice device, VkPipelineCache dstCache,
                      uint32_t srcCacheCount,
                      const VkPipelineCache *pSrcCaches) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->MergePipelineCaches(device, dstCache, srcCacheCount,
                                     pSrcCaches);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache,
                          uint32_t createInfoCount,
                          const VkGraphicsPipelineCreateInfo *pCreateInfos,
                          const VkAllocationCallbacks *pAllocator,
                          VkPipeline *pPipelines) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateGraphicsPipelines(device, pipelineCache, createInfoCount,
                                         pCreateInfos, pAllocator, pPipelines);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache,
                         uint32_t createInfoCount,
                         const VkComputePipelineCreateInfo *pCreateInfos,
                         const VkAllocationCallbacks *pAllocator,
                         VkPipeline *pPipelines) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateComputePipelines(device, pipelineCache, createInfoCount,
                                        pCreateInfos, pAllocator, pPipelines);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyPipeline(VkDevice device, VkPipeline pipeline,
                  const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroyPipeline(device, pipeline, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreatePipelineLayout(VkDevice device,
                       const VkPipelineLayoutCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkPipelineLayout *pPipelineLayout) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreatePipelineLayout(device, pCreateInfo, pAllocator,
                                      pPipelineLayout);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout,
                        const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroyPipelineLayout(device, pipelineLayout, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSampler(VkDevice device, const VkSamplerCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator, VkSampler *pSampler) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateSampler(device, pCreateInfo, pAllocator, pSampler);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroySampler(VkDevice device, VkSampler sampler,
                 const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroySampler(device, sampler, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDescriptorSetLayout(VkDevice device,
                            const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkDescriptorSetLayout *pSetLayout) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateDescriptorSetLayout(device, pCreateInfo, pAllocator,
                                           pSetLayout);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyDescriptorSetLayout(VkDevice device,
                             VkDescriptorSetLayout descriptorSetLayout,
                             const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroyDescriptorSetLayout(device, descriptorSetLayout, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDescriptorPool(VkDevice device,
                       const VkDescriptorPoolCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkDescriptorPool *pDescriptorPool) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateDescriptorPool(device, pCreateInfo, pAllocator,
                                      pDescriptorPool);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                        const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroyDescriptorPool(device, descriptorPool, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                      VkDescriptorPoolResetFlags flags) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->ResetDescriptorPool(device, descriptorPool, flags);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkAllocateDescriptorSets(VkDevice device,
                         const VkDescriptorSetAllocateInfo *pAllocateInfo,
                         VkDescriptorSet *pDescriptorSets) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->AllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool,
                     uint32_t descriptorSetCount,
                     const VkDescriptorSet *pDescriptorSets) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->FreeDescriptorSets(device, descriptorPool, descriptorSetCount,
                                    pDescriptorSets);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                       const VkWriteDescriptorSet *pDescriptorWrites,
                       uint32_t descriptorCopyCount,
                       const VkCopyDescriptorSet *pDescriptorCopies) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->UpdateDescriptorSets(device, descriptorWriteCount, pDescriptorWrites,
                               descriptorCopyCount, pDescriptorCopies);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkFramebuffer *pFramebuffer) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateFramebuffer(device, pCreateInfo, pAllocator,
                                   pFramebuffer);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer,
                     const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroyFramebuffer(device, framebuffer, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkRenderPass *pRenderPass) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass,
                    const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroyRenderPass(device, renderPass, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkGetRenderAreaGranularity(VkDevice device, VkRenderPass renderPass,
                           VkExtent2D *pGranularity) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->GetRenderAreaGranularity(device, renderPass, pGranularity);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkCommandPool *pCommandPool) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->CreateCommandPool(device, pCreateInfo, pAllocator,
                                   pCommandPool);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                     const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->DestroyCommandPool(device, commandPool, pAllocator);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkResetCommandPool(VkDevice device, VkCommandPool commandPool,
                   VkCommandPoolResetFlags flags) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    return disp->ResetCommandPool(device, commandPool, flags);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkAllocateCommandBuffers(VkDevice device,
                         const VkCommandBufferAllocateInfo *pAllocateInfo,
                         VkCommandBuffer *pCommandBuffers) {
    const VkLayerDispatchTable *disp;
    VkResult res;

    disp = loader_get_dispatch(device);

    res = disp->AllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
    if (res == VK_SUCCESS) {
        for (uint32_t i = 0; i < pAllocateInfo->commandBufferCount; i++) {
            if (pCommandBuffers[i]) {
                loader_init_dispatch(pCommandBuffers[i], disp);
            }
        }
    }

    return res;
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                     uint32_t commandBufferCount,
                     const VkCommandBuffer *pCommandBuffers) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(device);

    disp->FreeCommandBuffers(device, commandPool, commandBufferCount,
                             pCommandBuffers);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkBeginCommandBuffer(VkCommandBuffer commandBuffer,
                     const VkCommandBufferBeginInfo *pBeginInfo) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    return disp->BeginCommandBuffer(commandBuffer, pBeginInfo);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    return disp->EndCommandBuffer(commandBuffer);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkResetCommandBuffer(VkCommandBuffer commandBuffer,
                     VkCommandBufferResetFlags flags) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    return disp->ResetCommandBuffer(commandBuffer, flags);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBindPipeline(VkCommandBuffer commandBuffer,
                  VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport,
                 uint32_t viewportCount, const VkViewport *pViewports) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdSetViewport(commandBuffer, firstViewport, viewportCount,
                         pViewports);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor,
                uint32_t scissorCount, const VkRect2D *pScissors) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdSetLineWidth(commandBuffer, lineWidth);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetDepthBias(VkCommandBuffer commandBuffer, float depthBiasConstantFactor,
                  float depthBiasClamp, float depthBiasSlopeFactor) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdSetDepthBias(commandBuffer, depthBiasConstantFactor,
                          depthBiasClamp, depthBiasSlopeFactor);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetBlendConstants(VkCommandBuffer commandBuffer,
                       const float blendConstants[4]) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdSetBlendConstants(commandBuffer, blendConstants);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetDepthBounds(VkCommandBuffer commandBuffer, float minDepthBounds,
                    float maxDepthBounds) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdSetDepthBounds(commandBuffer, minDepthBounds, maxDepthBounds);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilCompareMask(VkCommandBuffer commandBuffer,
                           VkStencilFaceFlags faceMask, uint32_t compareMask) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdSetStencilCompareMask(commandBuffer, faceMask, compareMask);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilWriteMask(VkCommandBuffer commandBuffer,
                         VkStencilFaceFlags faceMask, uint32_t writeMask) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdSetStencilWriteMask(commandBuffer, faceMask, writeMask);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetStencilReference(VkCommandBuffer commandBuffer,
                         VkStencilFaceFlags faceMask, uint32_t reference) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdSetStencilReference(commandBuffer, faceMask, reference);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(
    VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
    VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount,
    const VkDescriptorSet *pDescriptorSets, uint32_t dynamicOffsetCount,
    const uint32_t *pDynamicOffsets) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout,
                                firstSet, descriptorSetCount, pDescriptorSets,
                                dynamicOffsetCount, pDynamicOffsets);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBindIndexBuffer(VkCommandBuffer commandBuffer, VkBuffer buffer,
                     VkDeviceSize offset, VkIndexType indexType) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdBindIndexBuffer(commandBuffer, buffer, offset, indexType);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBindVertexBuffers(VkCommandBuffer commandBuffer, uint32_t firstBinding,
                       uint32_t bindingCount, const VkBuffer *pBuffers,
                       const VkDeviceSize *pOffsets) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdBindVertexBuffers(commandBuffer, firstBinding, bindingCount,
                               pBuffers, pOffsets);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount,
          uint32_t instanceCount, uint32_t firstVertex,
          uint32_t firstInstance) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex,
                  firstInstance);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDrawIndexed(VkCommandBuffer commandBuffer, uint32_t indexCount,
                 uint32_t instanceCount, uint32_t firstIndex,
                 int32_t vertexOffset, uint32_t firstInstance) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex,
                         vertexOffset, firstInstance);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer,
                  VkDeviceSize offset, uint32_t drawCount, uint32_t stride) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdDrawIndirect(commandBuffer, buffer, offset, drawCount, stride);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer,
                         VkDeviceSize offset, uint32_t drawCount,
                         uint32_t stride) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdDrawIndexedIndirect(commandBuffer, buffer, offset, drawCount,
                                 stride);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y,
              uint32_t z) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdDispatch(commandBuffer, x, y, z);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer,
                      VkDeviceSize offset) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdDispatchIndirect(commandBuffer, buffer, offset);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer,
                VkBuffer dstBuffer, uint32_t regionCount,
                const VkBufferCopy *pRegions) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, regionCount,
                        pRegions);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage,
               VkImageLayout srcImageLayout, VkImage dstImage,
               VkImageLayout dstImageLayout, uint32_t regionCount,
               const VkImageCopy *pRegions) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdCopyImage(commandBuffer, srcImage, srcImageLayout, dstImage,
                       dstImageLayout, regionCount, pRegions);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage,
               VkImageLayout srcImageLayout, VkImage dstImage,
               VkImageLayout dstImageLayout, uint32_t regionCount,
               const VkImageBlit *pRegions, VkFilter filter) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdBlitImage(commandBuffer, srcImage, srcImageLayout, dstImage,
                       dstImageLayout, regionCount, pRegions, filter);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer,
                       VkImage dstImage, VkImageLayout dstImageLayout,
                       uint32_t regionCount,
                       const VkBufferImageCopy *pRegions) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage,
                               dstImageLayout, regionCount, pRegions);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage,
                       VkImageLayout srcImageLayout, VkBuffer dstBuffer,
                       uint32_t regionCount,
                       const VkBufferImageCopy *pRegions) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdCopyImageToBuffer(commandBuffer, srcImage, srcImageLayout,
                               dstBuffer, regionCount, pRegions);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdUpdateBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                  VkDeviceSize dstOffset, VkDeviceSize dataSize,
                  const uint32_t *pData) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdUpdateBuffer(commandBuffer, dstBuffer, dstOffset, dataSize, pData);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                VkDeviceSize dstOffset, VkDeviceSize size, uint32_t data) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdFillBuffer(commandBuffer, dstBuffer, dstOffset, size, data);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image,
                     VkImageLayout imageLayout, const VkClearColorValue *pColor,
                     uint32_t rangeCount,
                     const VkImageSubresourceRange *pRanges) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdClearColorImage(commandBuffer, image, imageLayout, pColor,
                             rangeCount, pRanges);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdClearDepthStencilImage(VkCommandBuffer commandBuffer, VkImage image,
                            VkImageLayout imageLayout,
                            const VkClearDepthStencilValue *pDepthStencil,
                            uint32_t rangeCount,
                            const VkImageSubresourceRange *pRanges) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdClearDepthStencilImage(commandBuffer, image, imageLayout,
                                    pDepthStencil, rangeCount, pRanges);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                      const VkClearAttachment *pAttachments, uint32_t rectCount,
                      const VkClearRect *pRects) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdClearAttachments(commandBuffer, attachmentCount, pAttachments,
                              rectCount, pRects);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                  VkImageLayout srcImageLayout, VkImage dstImage,
                  VkImageLayout dstImageLayout, uint32_t regionCount,
                  const VkImageResolve *pRegions) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdResolveImage(commandBuffer, srcImage, srcImageLayout, dstImage,
                          dstImageLayout, regionCount, pRegions);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event,
              VkPipelineStageFlags stageMask) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdSetEvent(commandBuffer, event, stageMask);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdResetEvent(VkCommandBuffer commandBuffer, VkEvent event,
                VkPipelineStageFlags stageMask) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdResetEvent(commandBuffer, event, stageMask);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount,
                const VkEvent *pEvents, VkPipelineStageFlags sourceStageMask,
                VkPipelineStageFlags dstStageMask, uint32_t memoryBarrierCount,
                const VkMemoryBarrier *pMemoryBarriers,
                uint32_t bufferMemoryBarrierCount,
                const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                uint32_t imageMemoryBarrierCount,
                const VkImageMemoryBarrier *pImageMemoryBarriers) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdWaitEvents(commandBuffer, eventCount, pEvents, sourceStageMask,
                        dstStageMask, memoryBarrierCount, pMemoryBarriers,
                        bufferMemoryBarrierCount, pBufferMemoryBarriers,
                        imageMemoryBarrierCount, pImageMemoryBarriers);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
    VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
    uint32_t memoryBarrierCount, const VkMemoryBarrier *pMemoryBarriers,
    uint32_t bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier *pBufferMemoryBarriers,
    uint32_t imageMemoryBarrierCount,
    const VkImageMemoryBarrier *pImageMemoryBarriers) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdPipelineBarrier(
        commandBuffer, srcStageMask, dstStageMask, dependencyFlags,
        memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
        pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBeginQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                uint32_t slot, VkFlags flags) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdBeginQuery(commandBuffer, queryPool, slot, flags);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdEndQuery(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
              uint32_t slot) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdEndQuery(commandBuffer, queryPool, slot);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                    uint32_t firstQuery, uint32_t queryCount) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdResetQueryPool(commandBuffer, queryPool, firstQuery, queryCount);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdWriteTimestamp(VkCommandBuffer commandBuffer,
                    VkPipelineStageFlagBits pipelineStage,
                    VkQueryPool queryPool, uint32_t slot) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdWriteTimestamp(commandBuffer, pipelineStage, queryPool, slot);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                          uint32_t firstQuery, uint32_t queryCount,
                          VkBuffer dstBuffer, VkDeviceSize dstOffset,
                          VkDeviceSize stride, VkFlags flags) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdCopyQueryPoolResults(commandBuffer, queryPool, firstQuery,
                                  queryCount, dstBuffer, dstOffset, stride,
                                  flags);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
                   VkShaderStageFlags stageFlags, uint32_t offset,
                   uint32_t size, const void *pValues) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdPushConstants(commandBuffer, layout, stageFlags, offset, size,
                           pValues);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdBeginRenderPass(VkCommandBuffer commandBuffer,
                     const VkRenderPassBeginInfo *pRenderPassBegin,
                     VkSubpassContents contents) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdNextSubpass(commandBuffer, contents);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdEndRenderPass(VkCommandBuffer commandBuffer) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdEndRenderPass(commandBuffer);
}

LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkCmdExecuteCommands(VkCommandBuffer commandBuffer,
                     uint32_t commandBuffersCount,
                     const VkCommandBuffer *pCommandBuffers) {
    const VkLayerDispatchTable *disp;

    disp = loader_get_dispatch(commandBuffer);

    disp->CmdExecuteCommands(commandBuffer, commandBuffersCount,
                             pCommandBuffers);
}
