/*
 * Copyright (c) 2015-2016 The Khronos Group Inc.
 * Copyright (c) 2015-2016 Valve Corporation
 * Copyright (c) 2015-2016 LunarG, Inc.
 * Copyright (C) 2015-2016 Google Inc.
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
 * Author: Courtney Goeltzenleuchter <courtney@LunarG.com>
 * Author: Jon Ashburn <jon@LunarG.com>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#ifndef WIN32
#include <signal.h>
#else
#endif
#include "vk_loader_platform.h"
#include "debug_report.h"
#include "vulkan/vk_layer.h"

typedef void(VKAPI_PTR *PFN_stringCallback)(char *message);

static const VkExtensionProperties debug_report_extension_info = {
    .extensionName = VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
    .specVersion = VK_EXT_DEBUG_REPORT_SPEC_VERSION,
};

void debug_report_add_instance_extensions(
    const struct loader_instance *inst,
    struct loader_extension_list *ext_list) {
    loader_add_to_ext_list(inst, ext_list, 1, &debug_report_extension_info);
}

void debug_report_create_instance(struct loader_instance *ptr_instance,
                                  const VkInstanceCreateInfo *pCreateInfo) {
    ptr_instance->debug_report_enabled = false;

    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                   VK_EXT_DEBUG_REPORT_EXTENSION_NAME) == 0) {
            ptr_instance->debug_report_enabled = true;
            return;
        }
    }
}

VkResult
util_CreateDebugReportCallback(struct loader_instance *inst,
                               VkDebugReportCallbackCreateInfoEXT *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkDebugReportCallbackEXT callback) {
    VkLayerDbgFunctionNode *pNewDbgFuncNode;
    if (pAllocator != NULL) {
        pNewDbgFuncNode = (VkLayerDbgFunctionNode *)pAllocator->pfnAllocation(
            pAllocator->pUserData, sizeof(VkLayerDbgFunctionNode),
            sizeof(int *), VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    } else {
        pNewDbgFuncNode = (VkLayerDbgFunctionNode *)loader_heap_alloc(
            inst, sizeof(VkLayerDbgFunctionNode),
            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
    }
    if (!pNewDbgFuncNode)
        return VK_ERROR_OUT_OF_HOST_MEMORY;

    pNewDbgFuncNode->msgCallback = callback;
    pNewDbgFuncNode->pfnMsgCallback = pCreateInfo->pfnCallback;
    pNewDbgFuncNode->msgFlags = pCreateInfo->flags;
    pNewDbgFuncNode->pUserData = pCreateInfo->pUserData;
    pNewDbgFuncNode->pNext = inst->DbgFunctionHead;
    inst->DbgFunctionHead = pNewDbgFuncNode;

    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL debug_report_CreateDebugReportCallback(
    VkInstance instance, VkDebugReportCallbackCreateInfoEXT *pCreateInfo,
    VkAllocationCallbacks *pAllocator, VkDebugReportCallbackEXT *pCallback) {
    struct loader_instance *inst = loader_get_instance(instance);
    loader_platform_thread_lock_mutex(&loader_lock);
    VkResult result = inst->disp->CreateDebugReportCallbackEXT(
        instance, pCreateInfo, pAllocator, pCallback);
    if (result == VK_SUCCESS) {
        result = util_CreateDebugReportCallback(inst, pCreateInfo, pAllocator,
                                                *pCallback);
    }
    loader_platform_thread_unlock_mutex(&loader_lock);
    return result;
}

// Utility function to handle reporting
VkBool32 util_DebugReportMessage(const struct loader_instance *inst,
                                 VkFlags msgFlags,
                                 VkDebugReportObjectTypeEXT objectType,
                                 uint64_t srcObject, size_t location,
                                 int32_t msgCode, const char *pLayerPrefix,
                                 const char *pMsg) {
    VkBool32 bail = false;
    VkLayerDbgFunctionNode *pTrav = inst->DbgFunctionHead;
    while (pTrav) {
        if (pTrav->msgFlags & msgFlags) {
            if (pTrav->pfnMsgCallback(msgFlags, objectType, srcObject, location,
                                      msgCode, pLayerPrefix, pMsg,
                                      pTrav->pUserData)) {
                bail = true;
            }
        }
        pTrav = pTrav->pNext;
    }

    return bail;
}

void util_DestroyDebugReportCallback(struct loader_instance *inst,
                                     VkDebugReportCallbackEXT callback,
                                     const VkAllocationCallbacks *pAllocator) {
    VkLayerDbgFunctionNode *pTrav = inst->DbgFunctionHead;
    VkLayerDbgFunctionNode *pPrev = pTrav;

    while (pTrav) {
        if (pTrav->msgCallback == callback) {
            pPrev->pNext = pTrav->pNext;
            if (inst->DbgFunctionHead == pTrav)
                inst->DbgFunctionHead = pTrav->pNext;
            if (pAllocator != NULL) {
                pAllocator->pfnFree(pAllocator->pUserData, pTrav);
            } else {
                loader_heap_free(inst, pTrav);
            }
            break;
        }
        pPrev = pTrav;
        pTrav = pTrav->pNext;
    }
}

// This utility (used by vkInstanceCreateInfo(), looks at a pNext chain.  It
// counts any VkDebugReportCallbackCreateInfoEXT structs that it finds.  It
// then allocates array that can hold that many structs, as well as that many
// VkDebugReportCallbackEXT handles.  It then copies each
// VkDebugReportCallbackCreateInfoEXT, and initializes each handle.
VkResult util_CopyDebugReportCreateInfos(
    const void *pChain, const VkAllocationCallbacks *pAllocator,
    uint32_t *num_callbacks, VkDebugReportCallbackCreateInfoEXT **infos,
    VkDebugReportCallbackEXT **callbacks) {
    uint32_t n = *num_callbacks = 0;

    // NOTE: The loader is not using pAllocator, and so this function doesn't
    // either.

    const void *pNext = pChain;
    while (pNext) {
        // 1st, count the number VkDebugReportCallbackCreateInfoEXT:
        if (((VkDebugReportCallbackCreateInfoEXT *)pNext)->sType ==
            VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT) {
            n++;
        }
        pNext = (void *)((VkDebugReportCallbackCreateInfoEXT *)pNext)->pNext;
    }
    if (n == 0) {
        return VK_SUCCESS;
    }

    // 2nd, allocate memory for each VkDebugReportCallbackCreateInfoEXT:
    VkDebugReportCallbackCreateInfoEXT *pInfos = *infos =
        ((VkDebugReportCallbackCreateInfoEXT *)malloc(
            n * sizeof(VkDebugReportCallbackCreateInfoEXT)));
    if (!pInfos) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    // 3rd, allocate memory for a unique handle for each callback:
    VkDebugReportCallbackEXT *pCallbacks = *callbacks =
        ((VkDebugReportCallbackEXT *)malloc(n *
                                            sizeof(VkDebugReportCallbackEXT)));
    if (!pCallbacks) {
        free(pInfos);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    // 4th, copy each VkDebugReportCallbackCreateInfoEXT for use by
    // vkDestroyInstance, and assign a unique handle to each callback (just
    // use the address of the copied VkDebugReportCallbackCreateInfoEXT):
    pNext = pChain;
    while (pNext) {
        if (((VkInstanceCreateInfo *)pNext)->sType ==
            VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT) {
            memcpy(pInfos, pNext, sizeof(VkDebugReportCallbackCreateInfoEXT));
            *pCallbacks++ = (VkDebugReportCallbackEXT)pInfos++;
        }
        pNext = (void *)((VkInstanceCreateInfo *)pNext)->pNext;
    }

    *num_callbacks = n;
    return VK_SUCCESS;
}

void util_FreeDebugReportCreateInfos(const VkAllocationCallbacks *pAllocator,
                                     VkDebugReportCallbackCreateInfoEXT *infos,
                                     VkDebugReportCallbackEXT *callbacks) {
    free(infos);
    free(callbacks);
}

VkResult util_CreateDebugReportCallbacks(
    struct loader_instance *inst, const VkAllocationCallbacks *pAllocator,
    uint32_t num_callbacks, VkDebugReportCallbackCreateInfoEXT *infos,
    VkDebugReportCallbackEXT *callbacks) {
    VkResult rtn = VK_SUCCESS;
    for (uint32_t i = 0; i < num_callbacks; i++) {
        rtn = util_CreateDebugReportCallback(inst, &infos[i], pAllocator,
                                             callbacks[i]);
        if (rtn != VK_SUCCESS) {
            for (uint32_t j = 0; j < i; j++) {
                util_DestroyDebugReportCallback(inst, callbacks[j], pAllocator);
            }
            return rtn;
        }
    }
    return rtn;
}

void util_DestroyDebugReportCallbacks(struct loader_instance *inst,
                                      const VkAllocationCallbacks *pAllocator,
                                      uint32_t num_callbacks,
                                      VkDebugReportCallbackEXT *callbacks) {
    for (uint32_t i = 0; i < num_callbacks; i++) {
        util_DestroyDebugReportCallback(inst, callbacks[i], pAllocator);
    }
}

static VKAPI_ATTR void VKAPI_CALL
debug_report_DestroyDebugReportCallback(VkInstance instance,
                                        VkDebugReportCallbackEXT callback,
                                        VkAllocationCallbacks *pAllocator) {
    struct loader_instance *inst = loader_get_instance(instance);
    loader_platform_thread_lock_mutex(&loader_lock);

    inst->disp->DestroyDebugReportCallbackEXT(instance, callback, pAllocator);

    util_DestroyDebugReportCallback(inst, callback, pAllocator);

    loader_platform_thread_unlock_mutex(&loader_lock);
}

static VKAPI_ATTR void VKAPI_CALL debug_report_DebugReportMessage(
    VkInstance instance, VkDebugReportFlagsEXT flags,
    VkDebugReportObjectTypeEXT objType, uint64_t object, size_t location,
    int32_t msgCode, const char *pLayerPrefix, const char *pMsg) {
    struct loader_instance *inst = loader_get_instance(instance);

    inst->disp->DebugReportMessageEXT(instance, flags, objType, object,
                                      location, msgCode, pLayerPrefix, pMsg);
}

/*
 * This is the instance chain terminator function
 * for CreateDebugReportCallback
 */

VKAPI_ATTR VkResult VKAPI_CALL terminator_CreateDebugReportCallback(
    VkInstance instance, const VkDebugReportCallbackCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugReportCallbackEXT *pCallback) {
    VkDebugReportCallbackEXT *icd_info;
    const struct loader_icd *icd;
    struct loader_instance *inst = (struct loader_instance *)instance;
    VkResult res = VK_SUCCESS;
    uint32_t storage_idx;

    icd_info = calloc(sizeof(VkDebugReportCallbackEXT), inst->total_icd_count);
    if (!icd_info) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    storage_idx = 0;
    for (icd = inst->icds; icd; icd = icd->next) {
        if (!icd->CreateDebugReportCallbackEXT) {
            continue;
        }

        res = icd->CreateDebugReportCallbackEXT(
            icd->instance, pCreateInfo, pAllocator, &icd_info[storage_idx]);

        if (res != VK_SUCCESS) {
            break;
        }
        storage_idx++;
    }

    /* roll back on errors */
    if (icd) {
        storage_idx = 0;
        for (icd = inst->icds; icd; icd = icd->next) {
            if (NULL == icd->DestroyDebugReportCallbackEXT) {
                continue;
            }

            if (icd_info[storage_idx]) {
                icd->DestroyDebugReportCallbackEXT(
                    icd->instance, icd_info[storage_idx], pAllocator);
            }
            storage_idx++;
        }

        return res;
    }

    *(VkDebugReportCallbackEXT **)pCallback = icd_info;

    return VK_SUCCESS;
}

/*
 * This is the instance chain terminator function
 * for DestroyDebugReportCallback
 */
VKAPI_ATTR void VKAPI_CALL
terminator_DestroyDebugReportCallback(VkInstance instance,
                                      VkDebugReportCallbackEXT callback,
                                      const VkAllocationCallbacks *pAllocator) {
    uint32_t storage_idx;
    VkDebugReportCallbackEXT *icd_info;
    const struct loader_icd *icd;

    struct loader_instance *inst = (struct loader_instance *)instance;
    icd_info = *(VkDebugReportCallbackEXT **)&callback;
    storage_idx = 0;
    for (icd = inst->icds; icd; icd = icd->next) {
        if (NULL == icd->DestroyDebugReportCallbackEXT) {
            continue;
        }

        if (icd_info[storage_idx]) {
            icd->DestroyDebugReportCallbackEXT(
                icd->instance, icd_info[storage_idx], pAllocator);
        }
        storage_idx++;
    }
}

/*
 * This is the instance chain terminator function
 * for DebugReportMessage
 */
VKAPI_ATTR void VKAPI_CALL
terminator_DebugReportMessage(VkInstance instance, VkDebugReportFlagsEXT flags,
                              VkDebugReportObjectTypeEXT objType,
                              uint64_t object, size_t location, int32_t msgCode,
                              const char *pLayerPrefix, const char *pMsg) {
    const struct loader_icd *icd;

    struct loader_instance *inst = (struct loader_instance *)instance;

    loader_platform_thread_lock_mutex(&loader_lock);
    for (icd = inst->icds; icd; icd = icd->next) {
        if (icd->DebugReportMessageEXT != NULL) {
            icd->DebugReportMessageEXT(icd->instance, flags, objType, object,
                                       location, msgCode, pLayerPrefix, pMsg);
        }
    }

    /*
     * Now that all ICDs have seen the message, call the necessary callbacks.
     * Ignoring "bail" return value as there is nothing to bail from at this
     * point.
     */

    util_DebugReportMessage(inst, flags, objType, object, location, msgCode,
                            pLayerPrefix, pMsg);

    loader_platform_thread_unlock_mutex(&loader_lock);
}

bool debug_report_instance_gpa(struct loader_instance *ptr_instance,
                               const char *name, void **addr) {
    // debug_report is currently advertised to be supported by the loader,
    // so always return the entry points if name matches and it's enabled
    *addr = NULL;

    if (!strcmp("vkCreateDebugReportCallbackEXT", name)) {
        *addr = ptr_instance->debug_report_enabled
                    ? (void *)debug_report_CreateDebugReportCallback
                    : NULL;
        return true;
    }
    if (!strcmp("vkDestroyDebugReportCallbackEXT", name)) {
        *addr = ptr_instance->debug_report_enabled
                    ? (void *)debug_report_DestroyDebugReportCallback
                    : NULL;
        return true;
    }
    if (!strcmp("vkDebugReportMessageEXT", name)) {
        *addr = ptr_instance->debug_report_enabled
                    ? (void *)debug_report_DebugReportMessage
                    : NULL;
        return true;
    }
    return false;
}
