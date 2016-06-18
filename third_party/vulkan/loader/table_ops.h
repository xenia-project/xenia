/*
 *
 * Copyright (c) 2015-2016 The Khronos Group Inc.
 * Copyright (c) 2015-2016 Valve Corporation
 * Copyright (c) 2015-2016 LunarG, Inc.
 * Copyright (C) 2016 Google Inc.
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
 * Author: Courtney Goeltzenleuchter <courtney@lunarg.com>
 * Author: Jon Ashburn <jon@lunarg.com>
 * Author: Ian Elliott <ian@LunarG.com>
 * Author: Tony Barbour <tony@LunarG.com>
 */

#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <string.h>
#include "loader.h"
#include "vk_loader_platform.h"

static VkResult vkDevExtError(VkDevice dev) {
    struct loader_device *found_dev;
    struct loader_icd *icd = loader_get_icd_and_device(dev, &found_dev);

    if (icd)
        loader_log(icd->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "Bad destination in loader trampoline dispatch,"
                   "Are layers and extensions that you are calling enabled?");
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static inline void
loader_init_device_dispatch_table(struct loader_dev_dispatch_table *dev_table,
                                  PFN_vkGetDeviceProcAddr gpa, VkDevice dev) {
    VkLayerDispatchTable *table = &dev_table->core_dispatch;
    for (uint32_t i = 0; i < MAX_NUM_DEV_EXTS; i++)
        dev_table->ext_dispatch.DevExt[i] = (PFN_vkDevExt)vkDevExtError;

    table->GetDeviceProcAddr =
        (PFN_vkGetDeviceProcAddr)gpa(dev, "vkGetDeviceProcAddr");
    table->DestroyDevice = (PFN_vkDestroyDevice)gpa(dev, "vkDestroyDevice");
    table->GetDeviceQueue = (PFN_vkGetDeviceQueue)gpa(dev, "vkGetDeviceQueue");
    table->QueueSubmit = (PFN_vkQueueSubmit)gpa(dev, "vkQueueSubmit");
    table->QueueWaitIdle = (PFN_vkQueueWaitIdle)gpa(dev, "vkQueueWaitIdle");
    table->DeviceWaitIdle = (PFN_vkDeviceWaitIdle)gpa(dev, "vkDeviceWaitIdle");
    table->AllocateMemory = (PFN_vkAllocateMemory)gpa(dev, "vkAllocateMemory");
    table->FreeMemory = (PFN_vkFreeMemory)gpa(dev, "vkFreeMemory");
    table->MapMemory = (PFN_vkMapMemory)gpa(dev, "vkMapMemory");
    table->UnmapMemory = (PFN_vkUnmapMemory)gpa(dev, "vkUnmapMemory");
    table->FlushMappedMemoryRanges =
        (PFN_vkFlushMappedMemoryRanges)gpa(dev, "vkFlushMappedMemoryRanges");
    table->InvalidateMappedMemoryRanges =
        (PFN_vkInvalidateMappedMemoryRanges)gpa(
            dev, "vkInvalidateMappedMemoryRanges");
    table->GetDeviceMemoryCommitment = (PFN_vkGetDeviceMemoryCommitment)gpa(
        dev, "vkGetDeviceMemoryCommitment");
    table->GetImageSparseMemoryRequirements =
        (PFN_vkGetImageSparseMemoryRequirements)gpa(
            dev, "vkGetImageSparseMemoryRequirements");
    table->GetBufferMemoryRequirements = (PFN_vkGetBufferMemoryRequirements)gpa(
        dev, "vkGetBufferMemoryRequirements");
    table->GetImageMemoryRequirements = (PFN_vkGetImageMemoryRequirements)gpa(
        dev, "vkGetImageMemoryRequirements");
    table->BindBufferMemory =
        (PFN_vkBindBufferMemory)gpa(dev, "vkBindBufferMemory");
    table->BindImageMemory =
        (PFN_vkBindImageMemory)gpa(dev, "vkBindImageMemory");
    table->QueueBindSparse =
        (PFN_vkQueueBindSparse)gpa(dev, "vkQueueBindSparse");
    table->CreateFence = (PFN_vkCreateFence)gpa(dev, "vkCreateFence");
    table->DestroyFence = (PFN_vkDestroyFence)gpa(dev, "vkDestroyFence");
    table->ResetFences = (PFN_vkResetFences)gpa(dev, "vkResetFences");
    table->GetFenceStatus = (PFN_vkGetFenceStatus)gpa(dev, "vkGetFenceStatus");
    table->WaitForFences = (PFN_vkWaitForFences)gpa(dev, "vkWaitForFences");
    table->CreateSemaphore =
        (PFN_vkCreateSemaphore)gpa(dev, "vkCreateSemaphore");
    table->DestroySemaphore =
        (PFN_vkDestroySemaphore)gpa(dev, "vkDestroySemaphore");
    table->CreateEvent = (PFN_vkCreateEvent)gpa(dev, "vkCreateEvent");
    table->DestroyEvent = (PFN_vkDestroyEvent)gpa(dev, "vkDestroyEvent");
    table->GetEventStatus = (PFN_vkGetEventStatus)gpa(dev, "vkGetEventStatus");
    table->SetEvent = (PFN_vkSetEvent)gpa(dev, "vkSetEvent");
    table->ResetEvent = (PFN_vkResetEvent)gpa(dev, "vkResetEvent");
    table->CreateQueryPool =
        (PFN_vkCreateQueryPool)gpa(dev, "vkCreateQueryPool");
    table->DestroyQueryPool =
        (PFN_vkDestroyQueryPool)gpa(dev, "vkDestroyQueryPool");
    table->GetQueryPoolResults =
        (PFN_vkGetQueryPoolResults)gpa(dev, "vkGetQueryPoolResults");
    table->CreateBuffer = (PFN_vkCreateBuffer)gpa(dev, "vkCreateBuffer");
    table->DestroyBuffer = (PFN_vkDestroyBuffer)gpa(dev, "vkDestroyBuffer");
    table->CreateBufferView =
        (PFN_vkCreateBufferView)gpa(dev, "vkCreateBufferView");
    table->DestroyBufferView =
        (PFN_vkDestroyBufferView)gpa(dev, "vkDestroyBufferView");
    table->CreateImage = (PFN_vkCreateImage)gpa(dev, "vkCreateImage");
    table->DestroyImage = (PFN_vkDestroyImage)gpa(dev, "vkDestroyImage");
    table->GetImageSubresourceLayout = (PFN_vkGetImageSubresourceLayout)gpa(
        dev, "vkGetImageSubresourceLayout");
    table->CreateImageView =
        (PFN_vkCreateImageView)gpa(dev, "vkCreateImageView");
    table->DestroyImageView =
        (PFN_vkDestroyImageView)gpa(dev, "vkDestroyImageView");
    table->CreateShaderModule =
        (PFN_vkCreateShaderModule)gpa(dev, "vkCreateShaderModule");
    table->DestroyShaderModule =
        (PFN_vkDestroyShaderModule)gpa(dev, "vkDestroyShaderModule");
    table->CreatePipelineCache =
        (PFN_vkCreatePipelineCache)gpa(dev, "vkCreatePipelineCache");
    table->DestroyPipelineCache =
        (PFN_vkDestroyPipelineCache)gpa(dev, "vkDestroyPipelineCache");
    table->GetPipelineCacheData =
        (PFN_vkGetPipelineCacheData)gpa(dev, "vkGetPipelineCacheData");
    table->MergePipelineCaches =
        (PFN_vkMergePipelineCaches)gpa(dev, "vkMergePipelineCaches");
    table->CreateGraphicsPipelines =
        (PFN_vkCreateGraphicsPipelines)gpa(dev, "vkCreateGraphicsPipelines");
    table->CreateComputePipelines =
        (PFN_vkCreateComputePipelines)gpa(dev, "vkCreateComputePipelines");
    table->DestroyPipeline =
        (PFN_vkDestroyPipeline)gpa(dev, "vkDestroyPipeline");
    table->CreatePipelineLayout =
        (PFN_vkCreatePipelineLayout)gpa(dev, "vkCreatePipelineLayout");
    table->DestroyPipelineLayout =
        (PFN_vkDestroyPipelineLayout)gpa(dev, "vkDestroyPipelineLayout");
    table->CreateSampler = (PFN_vkCreateSampler)gpa(dev, "vkCreateSampler");
    table->DestroySampler = (PFN_vkDestroySampler)gpa(dev, "vkDestroySampler");
    table->CreateDescriptorSetLayout = (PFN_vkCreateDescriptorSetLayout)gpa(
        dev, "vkCreateDescriptorSetLayout");
    table->DestroyDescriptorSetLayout = (PFN_vkDestroyDescriptorSetLayout)gpa(
        dev, "vkDestroyDescriptorSetLayout");
    table->CreateDescriptorPool =
        (PFN_vkCreateDescriptorPool)gpa(dev, "vkCreateDescriptorPool");
    table->DestroyDescriptorPool =
        (PFN_vkDestroyDescriptorPool)gpa(dev, "vkDestroyDescriptorPool");
    table->ResetDescriptorPool =
        (PFN_vkResetDescriptorPool)gpa(dev, "vkResetDescriptorPool");
    table->AllocateDescriptorSets =
        (PFN_vkAllocateDescriptorSets)gpa(dev, "vkAllocateDescriptorSets");
    table->FreeDescriptorSets =
        (PFN_vkFreeDescriptorSets)gpa(dev, "vkFreeDescriptorSets");
    table->UpdateDescriptorSets =
        (PFN_vkUpdateDescriptorSets)gpa(dev, "vkUpdateDescriptorSets");
    table->CreateFramebuffer =
        (PFN_vkCreateFramebuffer)gpa(dev, "vkCreateFramebuffer");
    table->DestroyFramebuffer =
        (PFN_vkDestroyFramebuffer)gpa(dev, "vkDestroyFramebuffer");
    table->CreateRenderPass =
        (PFN_vkCreateRenderPass)gpa(dev, "vkCreateRenderPass");
    table->DestroyRenderPass =
        (PFN_vkDestroyRenderPass)gpa(dev, "vkDestroyRenderPass");
    table->GetRenderAreaGranularity =
        (PFN_vkGetRenderAreaGranularity)gpa(dev, "vkGetRenderAreaGranularity");
    table->CreateCommandPool =
        (PFN_vkCreateCommandPool)gpa(dev, "vkCreateCommandPool");
    table->DestroyCommandPool =
        (PFN_vkDestroyCommandPool)gpa(dev, "vkDestroyCommandPool");
    table->ResetCommandPool =
        (PFN_vkResetCommandPool)gpa(dev, "vkResetCommandPool");
    table->AllocateCommandBuffers =
        (PFN_vkAllocateCommandBuffers)gpa(dev, "vkAllocateCommandBuffers");
    table->FreeCommandBuffers =
        (PFN_vkFreeCommandBuffers)gpa(dev, "vkFreeCommandBuffers");
    table->BeginCommandBuffer =
        (PFN_vkBeginCommandBuffer)gpa(dev, "vkBeginCommandBuffer");
    table->EndCommandBuffer =
        (PFN_vkEndCommandBuffer)gpa(dev, "vkEndCommandBuffer");
    table->ResetCommandBuffer =
        (PFN_vkResetCommandBuffer)gpa(dev, "vkResetCommandBuffer");
    table->CmdBindPipeline =
        (PFN_vkCmdBindPipeline)gpa(dev, "vkCmdBindPipeline");
    table->CmdSetViewport = (PFN_vkCmdSetViewport)gpa(dev, "vkCmdSetViewport");
    table->CmdSetScissor = (PFN_vkCmdSetScissor)gpa(dev, "vkCmdSetScissor");
    table->CmdSetLineWidth =
        (PFN_vkCmdSetLineWidth)gpa(dev, "vkCmdSetLineWidth");
    table->CmdSetDepthBias =
        (PFN_vkCmdSetDepthBias)gpa(dev, "vkCmdSetDepthBias");
    table->CmdSetBlendConstants =
        (PFN_vkCmdSetBlendConstants)gpa(dev, "vkCmdSetBlendConstants");
    table->CmdSetDepthBounds =
        (PFN_vkCmdSetDepthBounds)gpa(dev, "vkCmdSetDepthBounds");
    table->CmdSetStencilCompareMask =
        (PFN_vkCmdSetStencilCompareMask)gpa(dev, "vkCmdSetStencilCompareMask");
    table->CmdSetStencilWriteMask =
        (PFN_vkCmdSetStencilWriteMask)gpa(dev, "vkCmdSetStencilWriteMask");
    table->CmdSetStencilReference =
        (PFN_vkCmdSetStencilReference)gpa(dev, "vkCmdSetStencilReference");
    table->CmdBindDescriptorSets =
        (PFN_vkCmdBindDescriptorSets)gpa(dev, "vkCmdBindDescriptorSets");
    table->CmdBindVertexBuffers =
        (PFN_vkCmdBindVertexBuffers)gpa(dev, "vkCmdBindVertexBuffers");
    table->CmdBindIndexBuffer =
        (PFN_vkCmdBindIndexBuffer)gpa(dev, "vkCmdBindIndexBuffer");
    table->CmdDraw = (PFN_vkCmdDraw)gpa(dev, "vkCmdDraw");
    table->CmdDrawIndexed = (PFN_vkCmdDrawIndexed)gpa(dev, "vkCmdDrawIndexed");
    table->CmdDrawIndirect =
        (PFN_vkCmdDrawIndirect)gpa(dev, "vkCmdDrawIndirect");
    table->CmdDrawIndexedIndirect =
        (PFN_vkCmdDrawIndexedIndirect)gpa(dev, "vkCmdDrawIndexedIndirect");
    table->CmdDispatch = (PFN_vkCmdDispatch)gpa(dev, "vkCmdDispatch");
    table->CmdDispatchIndirect =
        (PFN_vkCmdDispatchIndirect)gpa(dev, "vkCmdDispatchIndirect");
    table->CmdCopyBuffer = (PFN_vkCmdCopyBuffer)gpa(dev, "vkCmdCopyBuffer");
    table->CmdCopyImage = (PFN_vkCmdCopyImage)gpa(dev, "vkCmdCopyImage");
    table->CmdBlitImage = (PFN_vkCmdBlitImage)gpa(dev, "vkCmdBlitImage");
    table->CmdCopyBufferToImage =
        (PFN_vkCmdCopyBufferToImage)gpa(dev, "vkCmdCopyBufferToImage");
    table->CmdCopyImageToBuffer =
        (PFN_vkCmdCopyImageToBuffer)gpa(dev, "vkCmdCopyImageToBuffer");
    table->CmdUpdateBuffer =
        (PFN_vkCmdUpdateBuffer)gpa(dev, "vkCmdUpdateBuffer");
    table->CmdFillBuffer = (PFN_vkCmdFillBuffer)gpa(dev, "vkCmdFillBuffer");
    table->CmdClearColorImage =
        (PFN_vkCmdClearColorImage)gpa(dev, "vkCmdClearColorImage");
    table->CmdClearDepthStencilImage = (PFN_vkCmdClearDepthStencilImage)gpa(
        dev, "vkCmdClearDepthStencilImage");
    table->CmdClearAttachments =
        (PFN_vkCmdClearAttachments)gpa(dev, "vkCmdClearAttachments");
    table->CmdResolveImage =
        (PFN_vkCmdResolveImage)gpa(dev, "vkCmdResolveImage");
    table->CmdSetEvent = (PFN_vkCmdSetEvent)gpa(dev, "vkCmdSetEvent");
    table->CmdResetEvent = (PFN_vkCmdResetEvent)gpa(dev, "vkCmdResetEvent");
    table->CmdWaitEvents = (PFN_vkCmdWaitEvents)gpa(dev, "vkCmdWaitEvents");
    table->CmdPipelineBarrier =
        (PFN_vkCmdPipelineBarrier)gpa(dev, "vkCmdPipelineBarrier");
    table->CmdBeginQuery = (PFN_vkCmdBeginQuery)gpa(dev, "vkCmdBeginQuery");
    table->CmdEndQuery = (PFN_vkCmdEndQuery)gpa(dev, "vkCmdEndQuery");
    table->CmdResetQueryPool =
        (PFN_vkCmdResetQueryPool)gpa(dev, "vkCmdResetQueryPool");
    table->CmdWriteTimestamp =
        (PFN_vkCmdWriteTimestamp)gpa(dev, "vkCmdWriteTimestamp");
    table->CmdCopyQueryPoolResults =
        (PFN_vkCmdCopyQueryPoolResults)gpa(dev, "vkCmdCopyQueryPoolResults");
    table->CmdPushConstants =
        (PFN_vkCmdPushConstants)gpa(dev, "vkCmdPushConstants");
    table->CmdBeginRenderPass =
        (PFN_vkCmdBeginRenderPass)gpa(dev, "vkCmdBeginRenderPass");
    table->CmdNextSubpass = (PFN_vkCmdNextSubpass)gpa(dev, "vkCmdNextSubpass");
    table->CmdEndRenderPass =
        (PFN_vkCmdEndRenderPass)gpa(dev, "vkCmdEndRenderPass");
    table->CmdExecuteCommands =
        (PFN_vkCmdExecuteCommands)gpa(dev, "vkCmdExecuteCommands");
}

static inline void loader_init_device_extension_dispatch_table(
    struct loader_dev_dispatch_table *dev_table, PFN_vkGetDeviceProcAddr gpa,
    VkDevice dev) {
    VkLayerDispatchTable *table = &dev_table->core_dispatch;
    table->AcquireNextImageKHR =
        (PFN_vkAcquireNextImageKHR)gpa(dev, "vkAcquireNextImageKHR");
    table->CreateSwapchainKHR =
        (PFN_vkCreateSwapchainKHR)gpa(dev, "vkCreateSwapchainKHR");
    table->DestroySwapchainKHR =
        (PFN_vkDestroySwapchainKHR)gpa(dev, "vkDestroySwapchainKHR");
    table->GetSwapchainImagesKHR =
        (PFN_vkGetSwapchainImagesKHR)gpa(dev, "vkGetSwapchainImagesKHR");
    table->QueuePresentKHR =
        (PFN_vkQueuePresentKHR)gpa(dev, "vkQueuePresentKHR");
}

static inline void *
loader_lookup_device_dispatch_table(const VkLayerDispatchTable *table,
                                    const char *name) {
    if (!name || name[0] != 'v' || name[1] != 'k')
        return NULL;

    name += 2;
    if (!strcmp(name, "GetDeviceProcAddr"))
        return (void *)table->GetDeviceProcAddr;
    if (!strcmp(name, "DestroyDevice"))
        return (void *)table->DestroyDevice;
    if (!strcmp(name, "GetDeviceQueue"))
        return (void *)table->GetDeviceQueue;
    if (!strcmp(name, "QueueSubmit"))
        return (void *)table->QueueSubmit;
    if (!strcmp(name, "QueueWaitIdle"))
        return (void *)table->QueueWaitIdle;
    if (!strcmp(name, "DeviceWaitIdle"))
        return (void *)table->DeviceWaitIdle;
    if (!strcmp(name, "AllocateMemory"))
        return (void *)table->AllocateMemory;
    if (!strcmp(name, "FreeMemory"))
        return (void *)table->FreeMemory;
    if (!strcmp(name, "MapMemory"))
        return (void *)table->MapMemory;
    if (!strcmp(name, "UnmapMemory"))
        return (void *)table->UnmapMemory;
    if (!strcmp(name, "FlushMappedMemoryRanges"))
        return (void *)table->FlushMappedMemoryRanges;
    if (!strcmp(name, "InvalidateMappedMemoryRanges"))
        return (void *)table->InvalidateMappedMemoryRanges;
    if (!strcmp(name, "GetDeviceMemoryCommitment"))
        return (void *)table->GetDeviceMemoryCommitment;
    if (!strcmp(name, "GetImageSparseMemoryRequirements"))
        return (void *)table->GetImageSparseMemoryRequirements;
    if (!strcmp(name, "GetBufferMemoryRequirements"))
        return (void *)table->GetBufferMemoryRequirements;
    if (!strcmp(name, "GetImageMemoryRequirements"))
        return (void *)table->GetImageMemoryRequirements;
    if (!strcmp(name, "BindBufferMemory"))
        return (void *)table->BindBufferMemory;
    if (!strcmp(name, "BindImageMemory"))
        return (void *)table->BindImageMemory;
    if (!strcmp(name, "QueueBindSparse"))
        return (void *)table->QueueBindSparse;
    if (!strcmp(name, "CreateFence"))
        return (void *)table->CreateFence;
    if (!strcmp(name, "DestroyFence"))
        return (void *)table->DestroyFence;
    if (!strcmp(name, "ResetFences"))
        return (void *)table->ResetFences;
    if (!strcmp(name, "GetFenceStatus"))
        return (void *)table->GetFenceStatus;
    if (!strcmp(name, "WaitForFences"))
        return (void *)table->WaitForFences;
    if (!strcmp(name, "CreateSemaphore"))
        return (void *)table->CreateSemaphore;
    if (!strcmp(name, "DestroySemaphore"))
        return (void *)table->DestroySemaphore;
    if (!strcmp(name, "CreateEvent"))
        return (void *)table->CreateEvent;
    if (!strcmp(name, "DestroyEvent"))
        return (void *)table->DestroyEvent;
    if (!strcmp(name, "GetEventStatus"))
        return (void *)table->GetEventStatus;
    if (!strcmp(name, "SetEvent"))
        return (void *)table->SetEvent;
    if (!strcmp(name, "ResetEvent"))
        return (void *)table->ResetEvent;
    if (!strcmp(name, "CreateQueryPool"))
        return (void *)table->CreateQueryPool;
    if (!strcmp(name, "DestroyQueryPool"))
        return (void *)table->DestroyQueryPool;
    if (!strcmp(name, "GetQueryPoolResults"))
        return (void *)table->GetQueryPoolResults;
    if (!strcmp(name, "CreateBuffer"))
        return (void *)table->CreateBuffer;
    if (!strcmp(name, "DestroyBuffer"))
        return (void *)table->DestroyBuffer;
    if (!strcmp(name, "CreateBufferView"))
        return (void *)table->CreateBufferView;
    if (!strcmp(name, "DestroyBufferView"))
        return (void *)table->DestroyBufferView;
    if (!strcmp(name, "CreateImage"))
        return (void *)table->CreateImage;
    if (!strcmp(name, "DestroyImage"))
        return (void *)table->DestroyImage;
    if (!strcmp(name, "GetImageSubresourceLayout"))
        return (void *)table->GetImageSubresourceLayout;
    if (!strcmp(name, "CreateImageView"))
        return (void *)table->CreateImageView;
    if (!strcmp(name, "DestroyImageView"))
        return (void *)table->DestroyImageView;
    if (!strcmp(name, "CreateShaderModule"))
        return (void *)table->CreateShaderModule;
    if (!strcmp(name, "DestroyShaderModule"))
        return (void *)table->DestroyShaderModule;
    if (!strcmp(name, "CreatePipelineCache"))
        return (void *)vkCreatePipelineCache;
    if (!strcmp(name, "DestroyPipelineCache"))
        return (void *)vkDestroyPipelineCache;
    if (!strcmp(name, "GetPipelineCacheData"))
        return (void *)vkGetPipelineCacheData;
    if (!strcmp(name, "MergePipelineCaches"))
        return (void *)vkMergePipelineCaches;
    if (!strcmp(name, "CreateGraphicsPipelines"))
        return (void *)vkCreateGraphicsPipelines;
    if (!strcmp(name, "CreateComputePipelines"))
        return (void *)vkCreateComputePipelines;
    if (!strcmp(name, "DestroyPipeline"))
        return (void *)table->DestroyPipeline;
    if (!strcmp(name, "CreatePipelineLayout"))
        return (void *)table->CreatePipelineLayout;
    if (!strcmp(name, "DestroyPipelineLayout"))
        return (void *)table->DestroyPipelineLayout;
    if (!strcmp(name, "CreateSampler"))
        return (void *)table->CreateSampler;
    if (!strcmp(name, "DestroySampler"))
        return (void *)table->DestroySampler;
    if (!strcmp(name, "CreateDescriptorSetLayout"))
        return (void *)table->CreateDescriptorSetLayout;
    if (!strcmp(name, "DestroyDescriptorSetLayout"))
        return (void *)table->DestroyDescriptorSetLayout;
    if (!strcmp(name, "CreateDescriptorPool"))
        return (void *)table->CreateDescriptorPool;
    if (!strcmp(name, "DestroyDescriptorPool"))
        return (void *)table->DestroyDescriptorPool;
    if (!strcmp(name, "ResetDescriptorPool"))
        return (void *)table->ResetDescriptorPool;
    if (!strcmp(name, "AllocateDescriptorSets"))
        return (void *)table->AllocateDescriptorSets;
    if (!strcmp(name, "FreeDescriptorSets"))
        return (void *)table->FreeDescriptorSets;
    if (!strcmp(name, "UpdateDescriptorSets"))
        return (void *)table->UpdateDescriptorSets;
    if (!strcmp(name, "CreateFramebuffer"))
        return (void *)table->CreateFramebuffer;
    if (!strcmp(name, "DestroyFramebuffer"))
        return (void *)table->DestroyFramebuffer;
    if (!strcmp(name, "CreateRenderPass"))
        return (void *)table->CreateRenderPass;
    if (!strcmp(name, "DestroyRenderPass"))
        return (void *)table->DestroyRenderPass;
    if (!strcmp(name, "GetRenderAreaGranularity"))
        return (void *)table->GetRenderAreaGranularity;
    if (!strcmp(name, "CreateCommandPool"))
        return (void *)table->CreateCommandPool;
    if (!strcmp(name, "DestroyCommandPool"))
        return (void *)table->DestroyCommandPool;
    if (!strcmp(name, "ResetCommandPool"))
        return (void *)table->ResetCommandPool;
    if (!strcmp(name, "AllocateCommandBuffers"))
        return (void *)table->AllocateCommandBuffers;
    if (!strcmp(name, "FreeCommandBuffers"))
        return (void *)table->FreeCommandBuffers;
    if (!strcmp(name, "BeginCommandBuffer"))
        return (void *)table->BeginCommandBuffer;
    if (!strcmp(name, "EndCommandBuffer"))
        return (void *)table->EndCommandBuffer;
    if (!strcmp(name, "ResetCommandBuffer"))
        return (void *)table->ResetCommandBuffer;
    if (!strcmp(name, "CmdBindPipeline"))
        return (void *)table->CmdBindPipeline;
    if (!strcmp(name, "CmdSetViewport"))
        return (void *)table->CmdSetViewport;
    if (!strcmp(name, "CmdSetScissor"))
        return (void *)table->CmdSetScissor;
    if (!strcmp(name, "CmdSetLineWidth"))
        return (void *)table->CmdSetLineWidth;
    if (!strcmp(name, "CmdSetDepthBias"))
        return (void *)table->CmdSetDepthBias;
    if (!strcmp(name, "CmdSetBlendConstants"))
        return (void *)table->CmdSetBlendConstants;
    if (!strcmp(name, "CmdSetDepthBounds"))
        return (void *)table->CmdSetDepthBounds;
    if (!strcmp(name, "CmdSetStencilCompareMask"))
        return (void *)table->CmdSetStencilCompareMask;
    if (!strcmp(name, "CmdSetStencilwriteMask"))
        return (void *)table->CmdSetStencilWriteMask;
    if (!strcmp(name, "CmdSetStencilReference"))
        return (void *)table->CmdSetStencilReference;
    if (!strcmp(name, "CmdBindDescriptorSets"))
        return (void *)table->CmdBindDescriptorSets;
    if (!strcmp(name, "CmdBindVertexBuffers"))
        return (void *)table->CmdBindVertexBuffers;
    if (!strcmp(name, "CmdBindIndexBuffer"))
        return (void *)table->CmdBindIndexBuffer;
    if (!strcmp(name, "CmdDraw"))
        return (void *)table->CmdDraw;
    if (!strcmp(name, "CmdDrawIndexed"))
        return (void *)table->CmdDrawIndexed;
    if (!strcmp(name, "CmdDrawIndirect"))
        return (void *)table->CmdDrawIndirect;
    if (!strcmp(name, "CmdDrawIndexedIndirect"))
        return (void *)table->CmdDrawIndexedIndirect;
    if (!strcmp(name, "CmdDispatch"))
        return (void *)table->CmdDispatch;
    if (!strcmp(name, "CmdDispatchIndirect"))
        return (void *)table->CmdDispatchIndirect;
    if (!strcmp(name, "CmdCopyBuffer"))
        return (void *)table->CmdCopyBuffer;
    if (!strcmp(name, "CmdCopyImage"))
        return (void *)table->CmdCopyImage;
    if (!strcmp(name, "CmdBlitImage"))
        return (void *)table->CmdBlitImage;
    if (!strcmp(name, "CmdCopyBufferToImage"))
        return (void *)table->CmdCopyBufferToImage;
    if (!strcmp(name, "CmdCopyImageToBuffer"))
        return (void *)table->CmdCopyImageToBuffer;
    if (!strcmp(name, "CmdUpdateBuffer"))
        return (void *)table->CmdUpdateBuffer;
    if (!strcmp(name, "CmdFillBuffer"))
        return (void *)table->CmdFillBuffer;
    if (!strcmp(name, "CmdClearColorImage"))
        return (void *)table->CmdClearColorImage;
    if (!strcmp(name, "CmdClearDepthStencilImage"))
        return (void *)table->CmdClearDepthStencilImage;
    if (!strcmp(name, "CmdClearAttachments"))
        return (void *)table->CmdClearAttachments;
    if (!strcmp(name, "CmdResolveImage"))
        return (void *)table->CmdResolveImage;
    if (!strcmp(name, "CmdSetEvent"))
        return (void *)table->CmdSetEvent;
    if (!strcmp(name, "CmdResetEvent"))
        return (void *)table->CmdResetEvent;
    if (!strcmp(name, "CmdWaitEvents"))
        return (void *)table->CmdWaitEvents;
    if (!strcmp(name, "CmdPipelineBarrier"))
        return (void *)table->CmdPipelineBarrier;
    if (!strcmp(name, "CmdBeginQuery"))
        return (void *)table->CmdBeginQuery;
    if (!strcmp(name, "CmdEndQuery"))
        return (void *)table->CmdEndQuery;
    if (!strcmp(name, "CmdResetQueryPool"))
        return (void *)table->CmdResetQueryPool;
    if (!strcmp(name, "CmdWriteTimestamp"))
        return (void *)table->CmdWriteTimestamp;
    if (!strcmp(name, "CmdCopyQueryPoolResults"))
        return (void *)table->CmdCopyQueryPoolResults;
    if (!strcmp(name, "CmdPushConstants"))
        return (void *)table->CmdPushConstants;
    if (!strcmp(name, "CmdBeginRenderPass"))
        return (void *)table->CmdBeginRenderPass;
    if (!strcmp(name, "CmdNextSubpass"))
        return (void *)table->CmdNextSubpass;
    if (!strcmp(name, "CmdEndRenderPass"))
        return (void *)table->CmdEndRenderPass;
    if (!strcmp(name, "CmdExecuteCommands"))
        return (void *)table->CmdExecuteCommands;

    return NULL;
}

static inline void
loader_init_instance_core_dispatch_table(VkLayerInstanceDispatchTable *table,
                                         PFN_vkGetInstanceProcAddr gpa,
                                         VkInstance inst) {
    table->GetInstanceProcAddr =
        (PFN_vkGetInstanceProcAddr)gpa(inst, "vkGetInstanceProcAddr");
    table->DestroyInstance =
        (PFN_vkDestroyInstance)gpa(inst, "vkDestroyInstance");
    table->EnumeratePhysicalDevices =
        (PFN_vkEnumeratePhysicalDevices)gpa(inst, "vkEnumeratePhysicalDevices");
    table->GetPhysicalDeviceFeatures = (PFN_vkGetPhysicalDeviceFeatures)gpa(
        inst, "vkGetPhysicalDeviceFeatures");
    table->GetPhysicalDeviceImageFormatProperties =
        (PFN_vkGetPhysicalDeviceImageFormatProperties)gpa(
            inst, "vkGetPhysicalDeviceImageFormatProperties");
    table->GetPhysicalDeviceFormatProperties =
        (PFN_vkGetPhysicalDeviceFormatProperties)gpa(
            inst, "vkGetPhysicalDeviceFormatProperties");
    table->GetPhysicalDeviceSparseImageFormatProperties =
        (PFN_vkGetPhysicalDeviceSparseImageFormatProperties)gpa(
            inst, "vkGetPhysicalDeviceSparseImageFormatProperties");
    table->GetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)gpa(
        inst, "vkGetPhysicalDeviceProperties");
    table->GetPhysicalDeviceQueueFamilyProperties =
        (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gpa(
            inst, "vkGetPhysicalDeviceQueueFamilyProperties");
    table->GetPhysicalDeviceMemoryProperties =
        (PFN_vkGetPhysicalDeviceMemoryProperties)gpa(
            inst, "vkGetPhysicalDeviceMemoryProperties");
    table->EnumerateDeviceExtensionProperties =
        (PFN_vkEnumerateDeviceExtensionProperties)gpa(
            inst, "vkEnumerateDeviceExtensionProperties");
    table->EnumerateDeviceLayerProperties =
        (PFN_vkEnumerateDeviceLayerProperties)gpa(
            inst, "vkEnumerateDeviceLayerProperties");
}

static inline void loader_init_instance_extension_dispatch_table(
    VkLayerInstanceDispatchTable *table, PFN_vkGetInstanceProcAddr gpa,
    VkInstance inst) {
    table->DestroySurfaceKHR =
        (PFN_vkDestroySurfaceKHR)gpa(inst, "vkDestroySurfaceKHR");
    table->CreateDebugReportCallbackEXT =
        (PFN_vkCreateDebugReportCallbackEXT)gpa(
            inst, "vkCreateDebugReportCallbackEXT");
    table->DestroyDebugReportCallbackEXT =
        (PFN_vkDestroyDebugReportCallbackEXT)gpa(
            inst, "vkDestroyDebugReportCallbackEXT");
    table->DebugReportMessageEXT =
        (PFN_vkDebugReportMessageEXT)gpa(inst, "vkDebugReportMessageEXT");
    table->GetPhysicalDeviceSurfaceSupportKHR =
        (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)gpa(
            inst, "vkGetPhysicalDeviceSurfaceSupportKHR");
    table->GetPhysicalDeviceSurfaceCapabilitiesKHR =
        (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)gpa(
            inst, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    table->GetPhysicalDeviceSurfaceFormatsKHR =
        (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)gpa(
            inst, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    table->GetPhysicalDeviceSurfacePresentModesKHR =
        (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)gpa(
            inst, "vkGetPhysicalDeviceSurfacePresentModesKHR");
#ifdef VK_USE_PLATFORM_MIR_KHR
    table->CreateMirSurfaceKHR =
        (PFN_vkCreateMirSurfaceKHR)gpa(inst, "vkCreateMirSurfaceKHR");
    table->GetPhysicalDeviceMirPresentationSupportKHR =
        (PFN_vkGetPhysicalDeviceMirPresentationSupportKHR)gpa(
            inst, "vkGetPhysicalDeviceMirPresentationSupportKHR");
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    table->CreateWaylandSurfaceKHR =
        (PFN_vkCreateWaylandSurfaceKHR)gpa(inst, "vkCreateWaylandSurfaceKHR");
    table->GetPhysicalDeviceWaylandPresentationSupportKHR =
        (PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR)gpa(
            inst, "vkGetPhysicalDeviceWaylandPresentationSupportKHR");
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
    table->CreateWin32SurfaceKHR =
        (PFN_vkCreateWin32SurfaceKHR)gpa(inst, "vkCreateWin32SurfaceKHR");
    table->GetPhysicalDeviceWin32PresentationSupportKHR =
        (PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR)gpa(
            inst, "vkGetPhysicalDeviceWin32PresentationSupportKHR");
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
    table->CreateXcbSurfaceKHR =
        (PFN_vkCreateXcbSurfaceKHR)gpa(inst, "vkCreateXcbSurfaceKHR");
    table->GetPhysicalDeviceXcbPresentationSupportKHR =
        (PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR)gpa(
            inst, "vkGetPhysicalDeviceXcbPresentationSupportKHR");
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
    table->CreateXlibSurfaceKHR =
        (PFN_vkCreateXlibSurfaceKHR)gpa(inst, "vkCreateXlibSurfaceKHR");
    table->GetPhysicalDeviceXlibPresentationSupportKHR =
        (PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR)gpa(
            inst, "vkGetPhysicalDeviceXlibPresentationSupportKHR");
#endif
    table->GetPhysicalDeviceDisplayPropertiesKHR =
        (PFN_vkGetPhysicalDeviceDisplayPropertiesKHR)gpa(
            inst, "vkGetPhysicalDeviceDisplayPropertiesKHR");
    table->GetPhysicalDeviceDisplayPlanePropertiesKHR =
        (PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR)gpa(
            inst, "vkGetPhysicalDeviceDisplayPlanePropertiesKHR");
    table->GetDisplayPlaneSupportedDisplaysKHR =
        (PFN_vkGetDisplayPlaneSupportedDisplaysKHR)gpa(
            inst, "vkGetDisplayPlaneSupportedDisplaysKHR");
    table->GetDisplayModePropertiesKHR = (PFN_vkGetDisplayModePropertiesKHR)gpa(
        inst, "vkGetDisplayModePropertiesKHR");
    table->CreateDisplayModeKHR =
        (PFN_vkCreateDisplayModeKHR)gpa(inst, "vkCreateDisplayModeKHR");
    table->GetDisplayPlaneCapabilitiesKHR =
        (PFN_vkGetDisplayPlaneCapabilitiesKHR)gpa(
            inst, "vkGetDisplayPlaneCapabilitiesKHR");
    table->CreateDisplayPlaneSurfaceKHR =
        (PFN_vkCreateDisplayPlaneSurfaceKHR)gpa(
            inst, "vkCreateDisplayPlaneSurfaceKHR");
}

static inline void *
loader_lookup_instance_dispatch_table(const VkLayerInstanceDispatchTable *table,
                                      const char *name, bool *found_name) {
    if (!name || name[0] != 'v' || name[1] != 'k') {
        *found_name = false;
        return NULL;
    }

    *found_name = true;
    name += 2;
    if (!strcmp(name, "DestroyInstance"))
        return (void *)table->DestroyInstance;
    if (!strcmp(name, "EnumeratePhysicalDevices"))
        return (void *)table->EnumeratePhysicalDevices;
    if (!strcmp(name, "GetPhysicalDeviceFeatures"))
        return (void *)table->GetPhysicalDeviceFeatures;
    if (!strcmp(name, "GetPhysicalDeviceImageFormatProperties"))
        return (void *)table->GetPhysicalDeviceImageFormatProperties;
    if (!strcmp(name, "GetPhysicalDeviceFormatProperties"))
        return (void *)table->GetPhysicalDeviceFormatProperties;
    if (!strcmp(name, "GetPhysicalDeviceSparseImageFormatProperties"))
        return (void *)table->GetPhysicalDeviceSparseImageFormatProperties;
    if (!strcmp(name, "GetPhysicalDeviceProperties"))
        return (void *)table->GetPhysicalDeviceProperties;
    if (!strcmp(name, "GetPhysicalDeviceQueueFamilyProperties"))
        return (void *)table->GetPhysicalDeviceQueueFamilyProperties;
    if (!strcmp(name, "GetPhysicalDeviceMemoryProperties"))
        return (void *)table->GetPhysicalDeviceMemoryProperties;
    if (!strcmp(name, "GetInstanceProcAddr"))
        return (void *)table->GetInstanceProcAddr;
    if (!strcmp(name, "EnumerateDeviceExtensionProperties"))
        return (void *)table->EnumerateDeviceExtensionProperties;
    if (!strcmp(name, "EnumerateDeviceLayerProperties"))
        return (void *)table->EnumerateDeviceLayerProperties;
    if (!strcmp(name, "DestroySurfaceKHR"))
        return (void *)table->DestroySurfaceKHR;
    if (!strcmp(name, "GetPhysicalDeviceSurfaceSupportKHR"))
        return (void *)table->GetPhysicalDeviceSurfaceSupportKHR;
    if (!strcmp(name, "GetPhysicalDeviceSurfaceCapabilitiesKHR"))
        return (void *)table->GetPhysicalDeviceSurfaceCapabilitiesKHR;
    if (!strcmp(name, "GetPhysicalDeviceSurfaceFormatsKHR"))
        return (void *)table->GetPhysicalDeviceSurfaceFormatsKHR;
    if (!strcmp(name, "GetPhysicalDeviceSurfacePresentModesKHR"))
        return (void *)table->GetPhysicalDeviceSurfacePresentModesKHR;
#ifdef VK_USE_PLATFORM_MIR_KHR
    if (!strcmp(name, "CreateMirSurfaceKHR"))
        return (void *)table->CreateMirSurfaceKHR;
    if (!strcmp(name, "GetPhysicalDeviceMirPresentationSupportKHR"))
        return (void *)table->GetPhysicalDeviceMirPresentationSupportKHR;
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    if (!strcmp(name, "CreateWaylandSurfaceKHR"))
        return (void *)table->CreateWaylandSurfaceKHR;
    if (!strcmp(name, "GetPhysicalDeviceWaylandPresentationSupportKHR"))
        return (void *)table->GetPhysicalDeviceWaylandPresentationSupportKHR;
#endif
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp(name, "CreateWin32SurfaceKHR"))
        return (void *)table->CreateWin32SurfaceKHR;
    if (!strcmp(name, "GetPhysicalDeviceWin32PresentationSupportKHR"))
        return (void *)table->GetPhysicalDeviceWin32PresentationSupportKHR;
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
    if (!strcmp(name, "CreateXcbSurfaceKHR"))
        return (void *)table->CreateXcbSurfaceKHR;
    if (!strcmp(name, "GetPhysicalDeviceXcbPresentationSupportKHR"))
        return (void *)table->GetPhysicalDeviceXcbPresentationSupportKHR;
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
    if (!strcmp(name, "CreateXlibSurfaceKHR"))
        return (void *)table->CreateXlibSurfaceKHR;
    if (!strcmp(name, "GetPhysicalDeviceXlibPresentationSupportKHR"))
        return (void *)table->GetPhysicalDeviceXlibPresentationSupportKHR;
#endif
    if (!strcmp(name, "GetPhysicalDeviceDisplayPropertiesKHR"))
        return (void *)table->GetPhysicalDeviceDisplayPropertiesKHR;
    if (!strcmp(name, "GetPhysicalDeviceDisplayPlanePropertiesKHR"))
        return (void *)table->GetPhysicalDeviceDisplayPlanePropertiesKHR;
    if (!strcmp(name, "GetDisplayPlaneSupportedDisplaysKHR"))
        return (void *)table->GetDisplayPlaneSupportedDisplaysKHR;
    if (!strcmp(name, "GetDisplayModePropertiesKHR"))
        return (void *)table->GetDisplayModePropertiesKHR;
    if (!strcmp(name, "CreateDisplayModeKHR"))
        return (void *)table->CreateDisplayModeKHR;
    if (!strcmp(name, "GetDisplayPlaneCapabilitiesKHR"))
        return (void *)table->GetDisplayPlaneCapabilitiesKHR;
    if (!strcmp(name, "CreateDisplayPlaneSurfaceKHR"))
        return (void *)table->CreateDisplayPlaneSurfaceKHR;

    if (!strcmp(name, "CreateDebugReportCallbackEXT"))
        return (void *)table->CreateDebugReportCallbackEXT;
    if (!strcmp(name, "DestroyDebugReportCallbackEXT"))
        return (void *)table->DestroyDebugReportCallbackEXT;
    if (!strcmp(name, "DebugReportMessageEXT"))
        return (void *)table->DebugReportMessageEXT;

    *found_name = false;
    return NULL;
}
