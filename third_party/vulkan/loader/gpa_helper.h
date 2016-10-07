/*
 *
 * Copyright (c) 2015 The Khronos Group Inc.
 * Copyright (c) 2015 Valve Corporation
 * Copyright (c) 2015 LunarG, Inc.
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
 * Author: Jon Ashburn <jon@lunarg.com>
 */

#include <string.h>
#include "debug_report.h"
#include "wsi.h"

static inline void *trampolineGetProcAddr(struct loader_instance *inst,
                                          const char *funcName) {
    // Don't include or check global functions
    if (!strcmp(funcName, "vkGetInstanceProcAddr"))
        return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (!strcmp(funcName, "vkDestroyInstance"))
        return (PFN_vkVoidFunction)vkDestroyInstance;
    if (!strcmp(funcName, "vkEnumeratePhysicalDevices"))
        return (PFN_vkVoidFunction)vkEnumeratePhysicalDevices;
    if (!strcmp(funcName, "vkGetPhysicalDeviceFeatures"))
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceFeatures;
    if (!strcmp(funcName, "vkGetPhysicalDeviceFormatProperties"))
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceFormatProperties;
    if (!strcmp(funcName, "vkGetPhysicalDeviceImageFormatProperties"))
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceImageFormatProperties;
    if (!strcmp(funcName, "vkGetPhysicalDeviceSparseImageFormatProperties"))
        return (
            PFN_vkVoidFunction)vkGetPhysicalDeviceSparseImageFormatProperties;
    if (!strcmp(funcName, "vkGetPhysicalDeviceProperties"))
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceProperties;
    if (!strcmp(funcName, "vkGetPhysicalDeviceQueueFamilyProperties"))
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceQueueFamilyProperties;
    if (!strcmp(funcName, "vkGetPhysicalDeviceMemoryProperties"))
        return (PFN_vkVoidFunction)vkGetPhysicalDeviceMemoryProperties;
    if (!strcmp(funcName, "vkEnumerateDeviceLayerProperties"))
        return (PFN_vkVoidFunction)vkEnumerateDeviceLayerProperties;
    if (!strcmp(funcName, "vkEnumerateDeviceExtensionProperties"))
        return (PFN_vkVoidFunction)vkEnumerateDeviceExtensionProperties;
    if (!strcmp(funcName, "vkCreateDevice"))
        return (PFN_vkVoidFunction)vkCreateDevice;
    if (!strcmp(funcName, "vkGetDeviceProcAddr"))
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    if (!strcmp(funcName, "vkDestroyDevice"))
        return (PFN_vkVoidFunction)vkDestroyDevice;
    if (!strcmp(funcName, "vkGetDeviceQueue"))
        return (PFN_vkVoidFunction)vkGetDeviceQueue;
    if (!strcmp(funcName, "vkQueueSubmit"))
        return (PFN_vkVoidFunction)vkQueueSubmit;
    if (!strcmp(funcName, "vkQueueWaitIdle"))
        return (PFN_vkVoidFunction)vkQueueWaitIdle;
    if (!strcmp(funcName, "vkDeviceWaitIdle"))
        return (PFN_vkVoidFunction)vkDeviceWaitIdle;
    if (!strcmp(funcName, "vkAllocateMemory"))
        return (PFN_vkVoidFunction)vkAllocateMemory;
    if (!strcmp(funcName, "vkFreeMemory"))
        return (PFN_vkVoidFunction)vkFreeMemory;
    if (!strcmp(funcName, "vkMapMemory"))
        return (PFN_vkVoidFunction)vkMapMemory;
    if (!strcmp(funcName, "vkUnmapMemory"))
        return (PFN_vkVoidFunction)vkUnmapMemory;
    if (!strcmp(funcName, "vkFlushMappedMemoryRanges"))
        return (PFN_vkVoidFunction)vkFlushMappedMemoryRanges;
    if (!strcmp(funcName, "vkInvalidateMappedMemoryRanges"))
        return (PFN_vkVoidFunction)vkInvalidateMappedMemoryRanges;
    if (!strcmp(funcName, "vkGetDeviceMemoryCommitment"))
        return (PFN_vkVoidFunction)vkGetDeviceMemoryCommitment;
    if (!strcmp(funcName, "vkGetImageSparseMemoryRequirements"))
        return (PFN_vkVoidFunction)vkGetImageSparseMemoryRequirements;
    if (!strcmp(funcName, "vkGetImageMemoryRequirements"))
        return (PFN_vkVoidFunction)vkGetImageMemoryRequirements;
    if (!strcmp(funcName, "vkGetBufferMemoryRequirements"))
        return (PFN_vkVoidFunction)vkGetBufferMemoryRequirements;
    if (!strcmp(funcName, "vkBindImageMemory"))
        return (PFN_vkVoidFunction)vkBindImageMemory;
    if (!strcmp(funcName, "vkBindBufferMemory"))
        return (PFN_vkVoidFunction)vkBindBufferMemory;
    if (!strcmp(funcName, "vkQueueBindSparse"))
        return (PFN_vkVoidFunction)vkQueueBindSparse;
    if (!strcmp(funcName, "vkCreateFence"))
        return (PFN_vkVoidFunction)vkCreateFence;
    if (!strcmp(funcName, "vkDestroyFence"))
        return (PFN_vkVoidFunction)vkDestroyFence;
    if (!strcmp(funcName, "vkGetFenceStatus"))
        return (PFN_vkVoidFunction)vkGetFenceStatus;
    if (!strcmp(funcName, "vkResetFences"))
        return (PFN_vkVoidFunction)vkResetFences;
    if (!strcmp(funcName, "vkWaitForFences"))
        return (PFN_vkVoidFunction)vkWaitForFences;
    if (!strcmp(funcName, "vkCreateSemaphore"))
        return (PFN_vkVoidFunction)vkCreateSemaphore;
    if (!strcmp(funcName, "vkDestroySemaphore"))
        return (PFN_vkVoidFunction)vkDestroySemaphore;
    if (!strcmp(funcName, "vkCreateEvent"))
        return (PFN_vkVoidFunction)vkCreateEvent;
    if (!strcmp(funcName, "vkDestroyEvent"))
        return (PFN_vkVoidFunction)vkDestroyEvent;
    if (!strcmp(funcName, "vkGetEventStatus"))
        return (PFN_vkVoidFunction)vkGetEventStatus;
    if (!strcmp(funcName, "vkSetEvent"))
        return (PFN_vkVoidFunction)vkSetEvent;
    if (!strcmp(funcName, "vkResetEvent"))
        return (PFN_vkVoidFunction)vkResetEvent;
    if (!strcmp(funcName, "vkCreateQueryPool"))
        return (PFN_vkVoidFunction)vkCreateQueryPool;
    if (!strcmp(funcName, "vkDestroyQueryPool"))
        return (PFN_vkVoidFunction)vkDestroyQueryPool;
    if (!strcmp(funcName, "vkGetQueryPoolResults"))
        return (PFN_vkVoidFunction)vkGetQueryPoolResults;
    if (!strcmp(funcName, "vkCreateBuffer"))
        return (PFN_vkVoidFunction)vkCreateBuffer;
    if (!strcmp(funcName, "vkDestroyBuffer"))
        return (PFN_vkVoidFunction)vkDestroyBuffer;
    if (!strcmp(funcName, "vkCreateBufferView"))
        return (PFN_vkVoidFunction)vkCreateBufferView;
    if (!strcmp(funcName, "vkDestroyBufferView"))
        return (PFN_vkVoidFunction)vkDestroyBufferView;
    if (!strcmp(funcName, "vkCreateImage"))
        return (PFN_vkVoidFunction)vkCreateImage;
    if (!strcmp(funcName, "vkDestroyImage"))
        return (PFN_vkVoidFunction)vkDestroyImage;
    if (!strcmp(funcName, "vkGetImageSubresourceLayout"))
        return (PFN_vkVoidFunction)vkGetImageSubresourceLayout;
    if (!strcmp(funcName, "vkCreateImageView"))
        return (PFN_vkVoidFunction)vkCreateImageView;
    if (!strcmp(funcName, "vkDestroyImageView"))
        return (PFN_vkVoidFunction)vkDestroyImageView;
    if (!strcmp(funcName, "vkCreateShaderModule"))
        return (PFN_vkVoidFunction)vkCreateShaderModule;
    if (!strcmp(funcName, "vkDestroyShaderModule"))
        return (PFN_vkVoidFunction)vkDestroyShaderModule;
    if (!strcmp(funcName, "vkCreatePipelineCache"))
        return (PFN_vkVoidFunction)vkCreatePipelineCache;
    if (!strcmp(funcName, "vkDestroyPipelineCache"))
        return (PFN_vkVoidFunction)vkDestroyPipelineCache;
    if (!strcmp(funcName, "vkGetPipelineCacheData"))
        return (PFN_vkVoidFunction)vkGetPipelineCacheData;
    if (!strcmp(funcName, "vkMergePipelineCaches"))
        return (PFN_vkVoidFunction)vkMergePipelineCaches;
    if (!strcmp(funcName, "vkCreateGraphicsPipelines"))
        return (PFN_vkVoidFunction)vkCreateGraphicsPipelines;
    if (!strcmp(funcName, "vkCreateComputePipelines"))
        return (PFN_vkVoidFunction)vkCreateComputePipelines;
    if (!strcmp(funcName, "vkDestroyPipeline"))
        return (PFN_vkVoidFunction)vkDestroyPipeline;
    if (!strcmp(funcName, "vkCreatePipelineLayout"))
        return (PFN_vkVoidFunction)vkCreatePipelineLayout;
    if (!strcmp(funcName, "vkDestroyPipelineLayout"))
        return (PFN_vkVoidFunction)vkDestroyPipelineLayout;
    if (!strcmp(funcName, "vkCreateSampler"))
        return (PFN_vkVoidFunction)vkCreateSampler;
    if (!strcmp(funcName, "vkDestroySampler"))
        return (PFN_vkVoidFunction)vkDestroySampler;
    if (!strcmp(funcName, "vkCreateDescriptorSetLayout"))
        return (PFN_vkVoidFunction)vkCreateDescriptorSetLayout;
    if (!strcmp(funcName, "vkDestroyDescriptorSetLayout"))
        return (PFN_vkVoidFunction)vkDestroyDescriptorSetLayout;
    if (!strcmp(funcName, "vkCreateDescriptorPool"))
        return (PFN_vkVoidFunction)vkCreateDescriptorPool;
    if (!strcmp(funcName, "vkDestroyDescriptorPool"))
        return (PFN_vkVoidFunction)vkDestroyDescriptorPool;
    if (!strcmp(funcName, "vkResetDescriptorPool"))
        return (PFN_vkVoidFunction)vkResetDescriptorPool;
    if (!strcmp(funcName, "vkAllocateDescriptorSets"))
        return (PFN_vkVoidFunction)vkAllocateDescriptorSets;
    if (!strcmp(funcName, "vkFreeDescriptorSets"))
        return (PFN_vkVoidFunction)vkFreeDescriptorSets;
    if (!strcmp(funcName, "vkUpdateDescriptorSets"))
        return (PFN_vkVoidFunction)vkUpdateDescriptorSets;
    if (!strcmp(funcName, "vkCreateFramebuffer"))
        return (PFN_vkVoidFunction)vkCreateFramebuffer;
    if (!strcmp(funcName, "vkDestroyFramebuffer"))
        return (PFN_vkVoidFunction)vkDestroyFramebuffer;
    if (!strcmp(funcName, "vkCreateRenderPass"))
        return (PFN_vkVoidFunction)vkCreateRenderPass;
    if (!strcmp(funcName, "vkDestroyRenderPass"))
        return (PFN_vkVoidFunction)vkDestroyRenderPass;
    if (!strcmp(funcName, "vkGetRenderAreaGranularity"))
        return (PFN_vkVoidFunction)vkGetRenderAreaGranularity;
    if (!strcmp(funcName, "vkCreateCommandPool"))
        return (PFN_vkVoidFunction)vkCreateCommandPool;
    if (!strcmp(funcName, "vkDestroyCommandPool"))
        return (PFN_vkVoidFunction)vkDestroyCommandPool;
    if (!strcmp(funcName, "vkResetCommandPool"))
        return (PFN_vkVoidFunction)vkResetCommandPool;
    if (!strcmp(funcName, "vkAllocateCommandBuffers"))
        return (PFN_vkVoidFunction)vkAllocateCommandBuffers;
    if (!strcmp(funcName, "vkFreeCommandBuffers"))
        return (PFN_vkVoidFunction)vkFreeCommandBuffers;
    if (!strcmp(funcName, "vkBeginCommandBuffer"))
        return (PFN_vkVoidFunction)vkBeginCommandBuffer;
    if (!strcmp(funcName, "vkEndCommandBuffer"))
        return (PFN_vkVoidFunction)vkEndCommandBuffer;
    if (!strcmp(funcName, "vkResetCommandBuffer"))
        return (PFN_vkVoidFunction)vkResetCommandBuffer;
    if (!strcmp(funcName, "vkCmdBindPipeline"))
        return (PFN_vkVoidFunction)vkCmdBindPipeline;
    if (!strcmp(funcName, "vkCmdBindDescriptorSets"))
        return (PFN_vkVoidFunction)vkCmdBindDescriptorSets;
    if (!strcmp(funcName, "vkCmdBindVertexBuffers"))
        return (PFN_vkVoidFunction)vkCmdBindVertexBuffers;
    if (!strcmp(funcName, "vkCmdBindIndexBuffer"))
        return (PFN_vkVoidFunction)vkCmdBindIndexBuffer;
    if (!strcmp(funcName, "vkCmdSetViewport"))
        return (PFN_vkVoidFunction)vkCmdSetViewport;
    if (!strcmp(funcName, "vkCmdSetScissor"))
        return (PFN_vkVoidFunction)vkCmdSetScissor;
    if (!strcmp(funcName, "vkCmdSetLineWidth"))
        return (PFN_vkVoidFunction)vkCmdSetLineWidth;
    if (!strcmp(funcName, "vkCmdSetDepthBias"))
        return (PFN_vkVoidFunction)vkCmdSetDepthBias;
    if (!strcmp(funcName, "vkCmdSetBlendConstants"))
        return (PFN_vkVoidFunction)vkCmdSetBlendConstants;
    if (!strcmp(funcName, "vkCmdSetDepthBounds"))
        return (PFN_vkVoidFunction)vkCmdSetDepthBounds;
    if (!strcmp(funcName, "vkCmdSetStencilCompareMask"))
        return (PFN_vkVoidFunction)vkCmdSetStencilCompareMask;
    if (!strcmp(funcName, "vkCmdSetStencilWriteMask"))
        return (PFN_vkVoidFunction)vkCmdSetStencilWriteMask;
    if (!strcmp(funcName, "vkCmdSetStencilReference"))
        return (PFN_vkVoidFunction)vkCmdSetStencilReference;
    if (!strcmp(funcName, "vkCmdDraw"))
        return (PFN_vkVoidFunction)vkCmdDraw;
    if (!strcmp(funcName, "vkCmdDrawIndexed"))
        return (PFN_vkVoidFunction)vkCmdDrawIndexed;
    if (!strcmp(funcName, "vkCmdDrawIndirect"))
        return (PFN_vkVoidFunction)vkCmdDrawIndirect;
    if (!strcmp(funcName, "vkCmdDrawIndexedIndirect"))
        return (PFN_vkVoidFunction)vkCmdDrawIndexedIndirect;
    if (!strcmp(funcName, "vkCmdDispatch"))
        return (PFN_vkVoidFunction)vkCmdDispatch;
    if (!strcmp(funcName, "vkCmdDispatchIndirect"))
        return (PFN_vkVoidFunction)vkCmdDispatchIndirect;
    if (!strcmp(funcName, "vkCmdCopyBuffer"))
        return (PFN_vkVoidFunction)vkCmdCopyBuffer;
    if (!strcmp(funcName, "vkCmdCopyImage"))
        return (PFN_vkVoidFunction)vkCmdCopyImage;
    if (!strcmp(funcName, "vkCmdBlitImage"))
        return (PFN_vkVoidFunction)vkCmdBlitImage;
    if (!strcmp(funcName, "vkCmdCopyBufferToImage"))
        return (PFN_vkVoidFunction)vkCmdCopyBufferToImage;
    if (!strcmp(funcName, "vkCmdCopyImageToBuffer"))
        return (PFN_vkVoidFunction)vkCmdCopyImageToBuffer;
    if (!strcmp(funcName, "vkCmdUpdateBuffer"))
        return (PFN_vkVoidFunction)vkCmdUpdateBuffer;
    if (!strcmp(funcName, "vkCmdFillBuffer"))
        return (PFN_vkVoidFunction)vkCmdFillBuffer;
    if (!strcmp(funcName, "vkCmdClearColorImage"))
        return (PFN_vkVoidFunction)vkCmdClearColorImage;
    if (!strcmp(funcName, "vkCmdClearDepthStencilImage"))
        return (PFN_vkVoidFunction)vkCmdClearDepthStencilImage;
    if (!strcmp(funcName, "vkCmdClearAttachments"))
        return (PFN_vkVoidFunction)vkCmdClearAttachments;
    if (!strcmp(funcName, "vkCmdResolveImage"))
        return (PFN_vkVoidFunction)vkCmdResolveImage;
    if (!strcmp(funcName, "vkCmdSetEvent"))
        return (PFN_vkVoidFunction)vkCmdSetEvent;
    if (!strcmp(funcName, "vkCmdResetEvent"))
        return (PFN_vkVoidFunction)vkCmdResetEvent;
    if (!strcmp(funcName, "vkCmdWaitEvents"))
        return (PFN_vkVoidFunction)vkCmdWaitEvents;
    if (!strcmp(funcName, "vkCmdPipelineBarrier"))
        return (PFN_vkVoidFunction)vkCmdPipelineBarrier;
    if (!strcmp(funcName, "vkCmdBeginQuery"))
        return (PFN_vkVoidFunction)vkCmdBeginQuery;
    if (!strcmp(funcName, "vkCmdEndQuery"))
        return (PFN_vkVoidFunction)vkCmdEndQuery;
    if (!strcmp(funcName, "vkCmdResetQueryPool"))
        return (PFN_vkVoidFunction)vkCmdResetQueryPool;
    if (!strcmp(funcName, "vkCmdWriteTimestamp"))
        return (PFN_vkVoidFunction)vkCmdWriteTimestamp;
    if (!strcmp(funcName, "vkCmdCopyQueryPoolResults"))
        return (PFN_vkVoidFunction)vkCmdCopyQueryPoolResults;
    if (!strcmp(funcName, "vkCmdPushConstants"))
        return (PFN_vkVoidFunction)vkCmdPushConstants;
    if (!strcmp(funcName, "vkCmdBeginRenderPass"))
        return (PFN_vkVoidFunction)vkCmdBeginRenderPass;
    if (!strcmp(funcName, "vkCmdNextSubpass"))
        return (PFN_vkVoidFunction)vkCmdNextSubpass;
    if (!strcmp(funcName, "vkCmdEndRenderPass"))
        return (PFN_vkVoidFunction)vkCmdEndRenderPass;
    if (!strcmp(funcName, "vkCmdExecuteCommands"))
        return (PFN_vkVoidFunction)vkCmdExecuteCommands;

    // Instance extensions
    void *addr;
    if (debug_report_instance_gpa(inst, funcName, &addr))
        return addr;

    if (wsi_swapchain_instance_gpa(inst, funcName, &addr))
        return addr;

    addr = loader_dev_ext_gpa(inst, funcName);
    return addr;
}

static inline void *globalGetProcAddr(const char *name) {
    if (!name || name[0] != 'v' || name[1] != 'k')
        return NULL;

    name += 2;
    if (!strcmp(name, "CreateInstance"))
        return (void *)vkCreateInstance;
    if (!strcmp(name, "EnumerateInstanceExtensionProperties"))
        return (void *)vkEnumerateInstanceExtensionProperties;
    if (!strcmp(name, "EnumerateInstanceLayerProperties"))
        return (void *)vkEnumerateInstanceLayerProperties;

    return NULL;
}

/* These functions require special handling by the loader.
*  They are not just generic trampoline code entrypoints.
*  Thus GPA must return loader entrypoint for these instead of first function
*  in the chain. */
static inline void *loader_non_passthrough_gipa(const char *name) {
    if (!name || name[0] != 'v' || name[1] != 'k')
        return NULL;

    name += 2;
    if (!strcmp(name, "CreateInstance"))
        return (void *)vkCreateInstance;
    if (!strcmp(name, "DestroyInstance"))
        return (void *)vkDestroyInstance;
    if (!strcmp(name, "GetDeviceProcAddr"))
        return (void *)vkGetDeviceProcAddr;
    // remove once no longer locks
    if (!strcmp(name, "EnumeratePhysicalDevices"))
        return (void *)vkEnumeratePhysicalDevices;
    if (!strcmp(name, "EnumerateDeviceExtensionProperties"))
        return (void *)vkEnumerateDeviceExtensionProperties;
    if (!strcmp(name, "EnumerateDeviceLayerProperties"))
        return (void *)vkEnumerateDeviceLayerProperties;
    if (!strcmp(name, "GetInstanceProcAddr"))
        return (void *)vkGetInstanceProcAddr;
    if (!strcmp(name, "CreateDevice"))
        return (void *)vkCreateDevice;

    return NULL;
}

static inline void *loader_non_passthrough_gdpa(const char *name) {
    if (!name || name[0] != 'v' || name[1] != 'k')
        return NULL;

    name += 2;

    if (!strcmp(name, "GetDeviceProcAddr"))
        return (void *)vkGetDeviceProcAddr;
    if (!strcmp(name, "DestroyDevice"))
        return (void *)vkDestroyDevice;
    if (!strcmp(name, "GetDeviceQueue"))
        return (void *)vkGetDeviceQueue;
    if (!strcmp(name, "AllocateCommandBuffers"))
        return (void *)vkAllocateCommandBuffers;

    return NULL;
}
