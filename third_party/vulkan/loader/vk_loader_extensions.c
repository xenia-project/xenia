// *** THIS FILE IS GENERATED - DO NOT EDIT ***
// See loader_extension_generator.py for modifications

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
 * Author: Mark Lobodzinski <mark@lunarg.com>
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
#include "extension_manual.h"

// Device extension error function
VKAPI_ATTR VkResult VKAPI_CALL vkDevExtError(VkDevice dev) {
    struct loader_device *found_dev;
    // The device going in is a trampoline device
    struct loader_icd_term *icd_term = loader_get_icd_and_device(dev, &found_dev, NULL);

    if (icd_term)
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "Bad destination in loader trampoline dispatch,"
                   "Are layers and extensions that you are calling enabled?");
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

VKAPI_ATTR bool VKAPI_CALL loader_icd_init_entries(struct loader_icd_term *icd_term, VkInstance inst,
                                                   const PFN_vkGetInstanceProcAddr fp_gipa) {

#define LOOKUP_GIPA(func, required)                                                        \
    do {                                                                                   \
        icd_term->dispatch.func = (PFN_vk##func)fp_gipa(inst, "vk" #func);                 \
        if (!icd_term->dispatch.func && required) {                                        \
            loader_log((struct loader_instance *)inst, VK_DEBUG_REPORT_WARNING_BIT_EXT, 0, \
                       loader_platform_get_proc_address_error("vk" #func));                \
            return false;                                                                  \
        }                                                                                  \
    } while (0)


    // ---- Core 1_0
    LOOKUP_GIPA(DestroyInstance, true);
    LOOKUP_GIPA(EnumeratePhysicalDevices, true);
    LOOKUP_GIPA(GetPhysicalDeviceFeatures, true);
    LOOKUP_GIPA(GetPhysicalDeviceFormatProperties, true);
    LOOKUP_GIPA(GetPhysicalDeviceImageFormatProperties, true);
    LOOKUP_GIPA(GetPhysicalDeviceProperties, true);
    LOOKUP_GIPA(GetPhysicalDeviceQueueFamilyProperties, true);
    LOOKUP_GIPA(GetPhysicalDeviceMemoryProperties, true);
    LOOKUP_GIPA(GetDeviceProcAddr, true);
    LOOKUP_GIPA(CreateDevice, true);
    LOOKUP_GIPA(EnumerateDeviceExtensionProperties, true);
    LOOKUP_GIPA(GetPhysicalDeviceSparseImageFormatProperties, true);

    // ---- VK_KHR_surface extension commands
    LOOKUP_GIPA(DestroySurfaceKHR, false);
    LOOKUP_GIPA(GetPhysicalDeviceSurfaceSupportKHR, false);
    LOOKUP_GIPA(GetPhysicalDeviceSurfaceCapabilitiesKHR, false);
    LOOKUP_GIPA(GetPhysicalDeviceSurfaceFormatsKHR, false);
    LOOKUP_GIPA(GetPhysicalDeviceSurfacePresentModesKHR, false);

    // ---- VK_KHR_swapchain extension commands
    LOOKUP_GIPA(CreateSwapchainKHR, false);

    // ---- VK_KHR_display extension commands
    LOOKUP_GIPA(GetPhysicalDeviceDisplayPropertiesKHR, false);
    LOOKUP_GIPA(GetPhysicalDeviceDisplayPlanePropertiesKHR, false);
    LOOKUP_GIPA(GetDisplayPlaneSupportedDisplaysKHR, false);
    LOOKUP_GIPA(GetDisplayModePropertiesKHR, false);
    LOOKUP_GIPA(CreateDisplayModeKHR, false);
    LOOKUP_GIPA(GetDisplayPlaneCapabilitiesKHR, false);
    LOOKUP_GIPA(CreateDisplayPlaneSurfaceKHR, false);

    // ---- VK_KHR_display_swapchain extension commands
    LOOKUP_GIPA(CreateSharedSwapchainsKHR, false);

    // ---- VK_KHR_xlib_surface extension commands
#ifdef VK_USE_PLATFORM_XLIB_KHR
    LOOKUP_GIPA(CreateXlibSurfaceKHR, false);
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    LOOKUP_GIPA(GetPhysicalDeviceXlibPresentationSupportKHR, false);
#endif // VK_USE_PLATFORM_XLIB_KHR

    // ---- VK_KHR_xcb_surface extension commands
#ifdef VK_USE_PLATFORM_XCB_KHR
    LOOKUP_GIPA(CreateXcbSurfaceKHR, false);
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    LOOKUP_GIPA(GetPhysicalDeviceXcbPresentationSupportKHR, false);
#endif // VK_USE_PLATFORM_XCB_KHR

    // ---- VK_KHR_wayland_surface extension commands
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    LOOKUP_GIPA(CreateWaylandSurfaceKHR, false);
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    LOOKUP_GIPA(GetPhysicalDeviceWaylandPresentationSupportKHR, false);
#endif // VK_USE_PLATFORM_WAYLAND_KHR

    // ---- VK_KHR_mir_surface extension commands
#ifdef VK_USE_PLATFORM_MIR_KHR
    LOOKUP_GIPA(CreateMirSurfaceKHR, false);
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
    LOOKUP_GIPA(GetPhysicalDeviceMirPresentationSupportKHR, false);
#endif // VK_USE_PLATFORM_MIR_KHR

    // ---- VK_KHR_android_surface extension commands
#ifdef VK_USE_PLATFORM_ANDROID_KHR
    LOOKUP_GIPA(CreateAndroidSurfaceKHR, false);
#endif // VK_USE_PLATFORM_ANDROID_KHR

    // ---- VK_KHR_win32_surface extension commands
#ifdef VK_USE_PLATFORM_WIN32_KHR
    LOOKUP_GIPA(CreateWin32SurfaceKHR, false);
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
    LOOKUP_GIPA(GetPhysicalDeviceWin32PresentationSupportKHR, false);
#endif // VK_USE_PLATFORM_WIN32_KHR

    // ---- VK_KHR_get_physical_device_properties2 extension commands
    LOOKUP_GIPA(GetPhysicalDeviceFeatures2KHR, false);
    LOOKUP_GIPA(GetPhysicalDeviceProperties2KHR, false);
    LOOKUP_GIPA(GetPhysicalDeviceFormatProperties2KHR, false);
    LOOKUP_GIPA(GetPhysicalDeviceImageFormatProperties2KHR, false);
    LOOKUP_GIPA(GetPhysicalDeviceQueueFamilyProperties2KHR, false);
    LOOKUP_GIPA(GetPhysicalDeviceMemoryProperties2KHR, false);
    LOOKUP_GIPA(GetPhysicalDeviceSparseImageFormatProperties2KHR, false);

    // ---- VK_KHR_external_memory_capabilities extension commands
    LOOKUP_GIPA(GetPhysicalDeviceExternalBufferPropertiesKHR, false);

    // ---- VK_KHR_external_semaphore_capabilities extension commands
    LOOKUP_GIPA(GetPhysicalDeviceExternalSemaphorePropertiesKHR, false);

    // ---- VK_KHR_external_fence_capabilities extension commands
    LOOKUP_GIPA(GetPhysicalDeviceExternalFencePropertiesKHR, false);

    // ---- VK_KHR_get_surface_capabilities2 extension commands
    LOOKUP_GIPA(GetPhysicalDeviceSurfaceCapabilities2KHR, false);
    LOOKUP_GIPA(GetPhysicalDeviceSurfaceFormats2KHR, false);

    // ---- VK_EXT_debug_report extension commands
    LOOKUP_GIPA(CreateDebugReportCallbackEXT, false);
    LOOKUP_GIPA(DestroyDebugReportCallbackEXT, false);
    LOOKUP_GIPA(DebugReportMessageEXT, false);

    // ---- VK_EXT_debug_marker extension commands
    LOOKUP_GIPA(DebugMarkerSetObjectTagEXT, false);
    LOOKUP_GIPA(DebugMarkerSetObjectNameEXT, false);

    // ---- VK_NV_external_memory_capabilities extension commands
    LOOKUP_GIPA(GetPhysicalDeviceExternalImageFormatPropertiesNV, false);

    // ---- VK_KHX_device_group extension commands
    LOOKUP_GIPA(GetDeviceGroupSurfacePresentModesKHX, false);
    LOOKUP_GIPA(GetPhysicalDevicePresentRectanglesKHX, false);

    // ---- VK_NN_vi_surface extension commands
#ifdef VK_USE_PLATFORM_VI_NN
    LOOKUP_GIPA(CreateViSurfaceNN, false);
#endif // VK_USE_PLATFORM_VI_NN

    // ---- VK_KHX_device_group_creation extension commands
    LOOKUP_GIPA(EnumeratePhysicalDeviceGroupsKHX, false);

    // ---- VK_NVX_device_generated_commands extension commands
    LOOKUP_GIPA(GetPhysicalDeviceGeneratedCommandsPropertiesNVX, false);

    // ---- VK_EXT_direct_mode_display extension commands
    LOOKUP_GIPA(ReleaseDisplayEXT, false);

    // ---- VK_EXT_acquire_xlib_display extension commands
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    LOOKUP_GIPA(AcquireXlibDisplayEXT, false);
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    LOOKUP_GIPA(GetRandROutputDisplayEXT, false);
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT

    // ---- VK_EXT_display_surface_counter extension commands
    LOOKUP_GIPA(GetPhysicalDeviceSurfaceCapabilities2EXT, false);

    // ---- VK_MVK_ios_surface extension commands
#ifdef VK_USE_PLATFORM_IOS_MVK
    LOOKUP_GIPA(CreateIOSSurfaceMVK, false);
#endif // VK_USE_PLATFORM_IOS_MVK

    // ---- VK_MVK_macos_surface extension commands
#ifdef VK_USE_PLATFORM_MACOS_MVK
    LOOKUP_GIPA(CreateMacOSSurfaceMVK, false);
#endif // VK_USE_PLATFORM_MACOS_MVK

    // ---- VK_EXT_sample_locations extension commands
    LOOKUP_GIPA(GetPhysicalDeviceMultisamplePropertiesEXT, false);

#undef LOOKUP_GIPA

    return true;
};

// Init Device function pointer dispatch table with core commands
VKAPI_ATTR void VKAPI_CALL loader_init_device_dispatch_table(struct loader_dev_dispatch_table *dev_table, PFN_vkGetDeviceProcAddr gpa,
                                                             VkDevice dev) {
    VkLayerDispatchTable *table = &dev_table->core_dispatch;
    for (uint32_t i = 0; i < MAX_NUM_UNKNOWN_EXTS; i++) dev_table->ext_dispatch.dev_ext[i] = (PFN_vkDevExt)vkDevExtError;

    // ---- Core 1_0 commands
    table->GetDeviceProcAddr = gpa;
    table->DestroyDevice = (PFN_vkDestroyDevice)gpa(dev, "vkDestroyDevice");
    table->GetDeviceQueue = (PFN_vkGetDeviceQueue)gpa(dev, "vkGetDeviceQueue");
    table->QueueSubmit = (PFN_vkQueueSubmit)gpa(dev, "vkQueueSubmit");
    table->QueueWaitIdle = (PFN_vkQueueWaitIdle)gpa(dev, "vkQueueWaitIdle");
    table->DeviceWaitIdle = (PFN_vkDeviceWaitIdle)gpa(dev, "vkDeviceWaitIdle");
    table->AllocateMemory = (PFN_vkAllocateMemory)gpa(dev, "vkAllocateMemory");
    table->FreeMemory = (PFN_vkFreeMemory)gpa(dev, "vkFreeMemory");
    table->MapMemory = (PFN_vkMapMemory)gpa(dev, "vkMapMemory");
    table->UnmapMemory = (PFN_vkUnmapMemory)gpa(dev, "vkUnmapMemory");
    table->FlushMappedMemoryRanges = (PFN_vkFlushMappedMemoryRanges)gpa(dev, "vkFlushMappedMemoryRanges");
    table->InvalidateMappedMemoryRanges = (PFN_vkInvalidateMappedMemoryRanges)gpa(dev, "vkInvalidateMappedMemoryRanges");
    table->GetDeviceMemoryCommitment = (PFN_vkGetDeviceMemoryCommitment)gpa(dev, "vkGetDeviceMemoryCommitment");
    table->BindBufferMemory = (PFN_vkBindBufferMemory)gpa(dev, "vkBindBufferMemory");
    table->BindImageMemory = (PFN_vkBindImageMemory)gpa(dev, "vkBindImageMemory");
    table->GetBufferMemoryRequirements = (PFN_vkGetBufferMemoryRequirements)gpa(dev, "vkGetBufferMemoryRequirements");
    table->GetImageMemoryRequirements = (PFN_vkGetImageMemoryRequirements)gpa(dev, "vkGetImageMemoryRequirements");
    table->GetImageSparseMemoryRequirements = (PFN_vkGetImageSparseMemoryRequirements)gpa(dev, "vkGetImageSparseMemoryRequirements");
    table->QueueBindSparse = (PFN_vkQueueBindSparse)gpa(dev, "vkQueueBindSparse");
    table->CreateFence = (PFN_vkCreateFence)gpa(dev, "vkCreateFence");
    table->DestroyFence = (PFN_vkDestroyFence)gpa(dev, "vkDestroyFence");
    table->ResetFences = (PFN_vkResetFences)gpa(dev, "vkResetFences");
    table->GetFenceStatus = (PFN_vkGetFenceStatus)gpa(dev, "vkGetFenceStatus");
    table->WaitForFences = (PFN_vkWaitForFences)gpa(dev, "vkWaitForFences");
    table->CreateSemaphore = (PFN_vkCreateSemaphore)gpa(dev, "vkCreateSemaphore");
    table->DestroySemaphore = (PFN_vkDestroySemaphore)gpa(dev, "vkDestroySemaphore");
    table->CreateEvent = (PFN_vkCreateEvent)gpa(dev, "vkCreateEvent");
    table->DestroyEvent = (PFN_vkDestroyEvent)gpa(dev, "vkDestroyEvent");
    table->GetEventStatus = (PFN_vkGetEventStatus)gpa(dev, "vkGetEventStatus");
    table->SetEvent = (PFN_vkSetEvent)gpa(dev, "vkSetEvent");
    table->ResetEvent = (PFN_vkResetEvent)gpa(dev, "vkResetEvent");
    table->CreateQueryPool = (PFN_vkCreateQueryPool)gpa(dev, "vkCreateQueryPool");
    table->DestroyQueryPool = (PFN_vkDestroyQueryPool)gpa(dev, "vkDestroyQueryPool");
    table->GetQueryPoolResults = (PFN_vkGetQueryPoolResults)gpa(dev, "vkGetQueryPoolResults");
    table->CreateBuffer = (PFN_vkCreateBuffer)gpa(dev, "vkCreateBuffer");
    table->DestroyBuffer = (PFN_vkDestroyBuffer)gpa(dev, "vkDestroyBuffer");
    table->CreateBufferView = (PFN_vkCreateBufferView)gpa(dev, "vkCreateBufferView");
    table->DestroyBufferView = (PFN_vkDestroyBufferView)gpa(dev, "vkDestroyBufferView");
    table->CreateImage = (PFN_vkCreateImage)gpa(dev, "vkCreateImage");
    table->DestroyImage = (PFN_vkDestroyImage)gpa(dev, "vkDestroyImage");
    table->GetImageSubresourceLayout = (PFN_vkGetImageSubresourceLayout)gpa(dev, "vkGetImageSubresourceLayout");
    table->CreateImageView = (PFN_vkCreateImageView)gpa(dev, "vkCreateImageView");
    table->DestroyImageView = (PFN_vkDestroyImageView)gpa(dev, "vkDestroyImageView");
    table->CreateShaderModule = (PFN_vkCreateShaderModule)gpa(dev, "vkCreateShaderModule");
    table->DestroyShaderModule = (PFN_vkDestroyShaderModule)gpa(dev, "vkDestroyShaderModule");
    table->CreatePipelineCache = (PFN_vkCreatePipelineCache)gpa(dev, "vkCreatePipelineCache");
    table->DestroyPipelineCache = (PFN_vkDestroyPipelineCache)gpa(dev, "vkDestroyPipelineCache");
    table->GetPipelineCacheData = (PFN_vkGetPipelineCacheData)gpa(dev, "vkGetPipelineCacheData");
    table->MergePipelineCaches = (PFN_vkMergePipelineCaches)gpa(dev, "vkMergePipelineCaches");
    table->CreateGraphicsPipelines = (PFN_vkCreateGraphicsPipelines)gpa(dev, "vkCreateGraphicsPipelines");
    table->CreateComputePipelines = (PFN_vkCreateComputePipelines)gpa(dev, "vkCreateComputePipelines");
    table->DestroyPipeline = (PFN_vkDestroyPipeline)gpa(dev, "vkDestroyPipeline");
    table->CreatePipelineLayout = (PFN_vkCreatePipelineLayout)gpa(dev, "vkCreatePipelineLayout");
    table->DestroyPipelineLayout = (PFN_vkDestroyPipelineLayout)gpa(dev, "vkDestroyPipelineLayout");
    table->CreateSampler = (PFN_vkCreateSampler)gpa(dev, "vkCreateSampler");
    table->DestroySampler = (PFN_vkDestroySampler)gpa(dev, "vkDestroySampler");
    table->CreateDescriptorSetLayout = (PFN_vkCreateDescriptorSetLayout)gpa(dev, "vkCreateDescriptorSetLayout");
    table->DestroyDescriptorSetLayout = (PFN_vkDestroyDescriptorSetLayout)gpa(dev, "vkDestroyDescriptorSetLayout");
    table->CreateDescriptorPool = (PFN_vkCreateDescriptorPool)gpa(dev, "vkCreateDescriptorPool");
    table->DestroyDescriptorPool = (PFN_vkDestroyDescriptorPool)gpa(dev, "vkDestroyDescriptorPool");
    table->ResetDescriptorPool = (PFN_vkResetDescriptorPool)gpa(dev, "vkResetDescriptorPool");
    table->AllocateDescriptorSets = (PFN_vkAllocateDescriptorSets)gpa(dev, "vkAllocateDescriptorSets");
    table->FreeDescriptorSets = (PFN_vkFreeDescriptorSets)gpa(dev, "vkFreeDescriptorSets");
    table->UpdateDescriptorSets = (PFN_vkUpdateDescriptorSets)gpa(dev, "vkUpdateDescriptorSets");
    table->CreateFramebuffer = (PFN_vkCreateFramebuffer)gpa(dev, "vkCreateFramebuffer");
    table->DestroyFramebuffer = (PFN_vkDestroyFramebuffer)gpa(dev, "vkDestroyFramebuffer");
    table->CreateRenderPass = (PFN_vkCreateRenderPass)gpa(dev, "vkCreateRenderPass");
    table->DestroyRenderPass = (PFN_vkDestroyRenderPass)gpa(dev, "vkDestroyRenderPass");
    table->GetRenderAreaGranularity = (PFN_vkGetRenderAreaGranularity)gpa(dev, "vkGetRenderAreaGranularity");
    table->CreateCommandPool = (PFN_vkCreateCommandPool)gpa(dev, "vkCreateCommandPool");
    table->DestroyCommandPool = (PFN_vkDestroyCommandPool)gpa(dev, "vkDestroyCommandPool");
    table->ResetCommandPool = (PFN_vkResetCommandPool)gpa(dev, "vkResetCommandPool");
    table->AllocateCommandBuffers = (PFN_vkAllocateCommandBuffers)gpa(dev, "vkAllocateCommandBuffers");
    table->FreeCommandBuffers = (PFN_vkFreeCommandBuffers)gpa(dev, "vkFreeCommandBuffers");
    table->BeginCommandBuffer = (PFN_vkBeginCommandBuffer)gpa(dev, "vkBeginCommandBuffer");
    table->EndCommandBuffer = (PFN_vkEndCommandBuffer)gpa(dev, "vkEndCommandBuffer");
    table->ResetCommandBuffer = (PFN_vkResetCommandBuffer)gpa(dev, "vkResetCommandBuffer");
    table->CmdBindPipeline = (PFN_vkCmdBindPipeline)gpa(dev, "vkCmdBindPipeline");
    table->CmdSetViewport = (PFN_vkCmdSetViewport)gpa(dev, "vkCmdSetViewport");
    table->CmdSetScissor = (PFN_vkCmdSetScissor)gpa(dev, "vkCmdSetScissor");
    table->CmdSetLineWidth = (PFN_vkCmdSetLineWidth)gpa(dev, "vkCmdSetLineWidth");
    table->CmdSetDepthBias = (PFN_vkCmdSetDepthBias)gpa(dev, "vkCmdSetDepthBias");
    table->CmdSetBlendConstants = (PFN_vkCmdSetBlendConstants)gpa(dev, "vkCmdSetBlendConstants");
    table->CmdSetDepthBounds = (PFN_vkCmdSetDepthBounds)gpa(dev, "vkCmdSetDepthBounds");
    table->CmdSetStencilCompareMask = (PFN_vkCmdSetStencilCompareMask)gpa(dev, "vkCmdSetStencilCompareMask");
    table->CmdSetStencilWriteMask = (PFN_vkCmdSetStencilWriteMask)gpa(dev, "vkCmdSetStencilWriteMask");
    table->CmdSetStencilReference = (PFN_vkCmdSetStencilReference)gpa(dev, "vkCmdSetStencilReference");
    table->CmdBindDescriptorSets = (PFN_vkCmdBindDescriptorSets)gpa(dev, "vkCmdBindDescriptorSets");
    table->CmdBindIndexBuffer = (PFN_vkCmdBindIndexBuffer)gpa(dev, "vkCmdBindIndexBuffer");
    table->CmdBindVertexBuffers = (PFN_vkCmdBindVertexBuffers)gpa(dev, "vkCmdBindVertexBuffers");
    table->CmdDraw = (PFN_vkCmdDraw)gpa(dev, "vkCmdDraw");
    table->CmdDrawIndexed = (PFN_vkCmdDrawIndexed)gpa(dev, "vkCmdDrawIndexed");
    table->CmdDrawIndirect = (PFN_vkCmdDrawIndirect)gpa(dev, "vkCmdDrawIndirect");
    table->CmdDrawIndexedIndirect = (PFN_vkCmdDrawIndexedIndirect)gpa(dev, "vkCmdDrawIndexedIndirect");
    table->CmdDispatch = (PFN_vkCmdDispatch)gpa(dev, "vkCmdDispatch");
    table->CmdDispatchIndirect = (PFN_vkCmdDispatchIndirect)gpa(dev, "vkCmdDispatchIndirect");
    table->CmdCopyBuffer = (PFN_vkCmdCopyBuffer)gpa(dev, "vkCmdCopyBuffer");
    table->CmdCopyImage = (PFN_vkCmdCopyImage)gpa(dev, "vkCmdCopyImage");
    table->CmdBlitImage = (PFN_vkCmdBlitImage)gpa(dev, "vkCmdBlitImage");
    table->CmdCopyBufferToImage = (PFN_vkCmdCopyBufferToImage)gpa(dev, "vkCmdCopyBufferToImage");
    table->CmdCopyImageToBuffer = (PFN_vkCmdCopyImageToBuffer)gpa(dev, "vkCmdCopyImageToBuffer");
    table->CmdUpdateBuffer = (PFN_vkCmdUpdateBuffer)gpa(dev, "vkCmdUpdateBuffer");
    table->CmdFillBuffer = (PFN_vkCmdFillBuffer)gpa(dev, "vkCmdFillBuffer");
    table->CmdClearColorImage = (PFN_vkCmdClearColorImage)gpa(dev, "vkCmdClearColorImage");
    table->CmdClearDepthStencilImage = (PFN_vkCmdClearDepthStencilImage)gpa(dev, "vkCmdClearDepthStencilImage");
    table->CmdClearAttachments = (PFN_vkCmdClearAttachments)gpa(dev, "vkCmdClearAttachments");
    table->CmdResolveImage = (PFN_vkCmdResolveImage)gpa(dev, "vkCmdResolveImage");
    table->CmdSetEvent = (PFN_vkCmdSetEvent)gpa(dev, "vkCmdSetEvent");
    table->CmdResetEvent = (PFN_vkCmdResetEvent)gpa(dev, "vkCmdResetEvent");
    table->CmdWaitEvents = (PFN_vkCmdWaitEvents)gpa(dev, "vkCmdWaitEvents");
    table->CmdPipelineBarrier = (PFN_vkCmdPipelineBarrier)gpa(dev, "vkCmdPipelineBarrier");
    table->CmdBeginQuery = (PFN_vkCmdBeginQuery)gpa(dev, "vkCmdBeginQuery");
    table->CmdEndQuery = (PFN_vkCmdEndQuery)gpa(dev, "vkCmdEndQuery");
    table->CmdResetQueryPool = (PFN_vkCmdResetQueryPool)gpa(dev, "vkCmdResetQueryPool");
    table->CmdWriteTimestamp = (PFN_vkCmdWriteTimestamp)gpa(dev, "vkCmdWriteTimestamp");
    table->CmdCopyQueryPoolResults = (PFN_vkCmdCopyQueryPoolResults)gpa(dev, "vkCmdCopyQueryPoolResults");
    table->CmdPushConstants = (PFN_vkCmdPushConstants)gpa(dev, "vkCmdPushConstants");
    table->CmdBeginRenderPass = (PFN_vkCmdBeginRenderPass)gpa(dev, "vkCmdBeginRenderPass");
    table->CmdNextSubpass = (PFN_vkCmdNextSubpass)gpa(dev, "vkCmdNextSubpass");
    table->CmdEndRenderPass = (PFN_vkCmdEndRenderPass)gpa(dev, "vkCmdEndRenderPass");
    table->CmdExecuteCommands = (PFN_vkCmdExecuteCommands)gpa(dev, "vkCmdExecuteCommands");
}

// Init Device function pointer dispatch table with extension commands
VKAPI_ATTR void VKAPI_CALL loader_init_device_extension_dispatch_table(struct loader_dev_dispatch_table *dev_table,
                                                                       PFN_vkGetDeviceProcAddr gpa, VkDevice dev) {
    VkLayerDispatchTable *table = &dev_table->core_dispatch;

    // ---- VK_KHR_swapchain extension commands
    table->CreateSwapchainKHR = (PFN_vkCreateSwapchainKHR)gpa(dev, "vkCreateSwapchainKHR");
    table->DestroySwapchainKHR = (PFN_vkDestroySwapchainKHR)gpa(dev, "vkDestroySwapchainKHR");
    table->GetSwapchainImagesKHR = (PFN_vkGetSwapchainImagesKHR)gpa(dev, "vkGetSwapchainImagesKHR");
    table->AcquireNextImageKHR = (PFN_vkAcquireNextImageKHR)gpa(dev, "vkAcquireNextImageKHR");
    table->QueuePresentKHR = (PFN_vkQueuePresentKHR)gpa(dev, "vkQueuePresentKHR");

    // ---- VK_KHR_display_swapchain extension commands
    table->CreateSharedSwapchainsKHR = (PFN_vkCreateSharedSwapchainsKHR)gpa(dev, "vkCreateSharedSwapchainsKHR");

    // ---- VK_KHR_maintenance1 extension commands
    table->TrimCommandPoolKHR = (PFN_vkTrimCommandPoolKHR)gpa(dev, "vkTrimCommandPoolKHR");

    // ---- VK_KHR_external_memory_win32 extension commands
#ifdef VK_USE_PLATFORM_WIN32_KHR
    table->GetMemoryWin32HandleKHR = (PFN_vkGetMemoryWin32HandleKHR)gpa(dev, "vkGetMemoryWin32HandleKHR");
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
    table->GetMemoryWin32HandlePropertiesKHR = (PFN_vkGetMemoryWin32HandlePropertiesKHR)gpa(dev, "vkGetMemoryWin32HandlePropertiesKHR");
#endif // VK_USE_PLATFORM_WIN32_KHR

    // ---- VK_KHR_external_memory_fd extension commands
    table->GetMemoryFdKHR = (PFN_vkGetMemoryFdKHR)gpa(dev, "vkGetMemoryFdKHR");
    table->GetMemoryFdPropertiesKHR = (PFN_vkGetMemoryFdPropertiesKHR)gpa(dev, "vkGetMemoryFdPropertiesKHR");

    // ---- VK_KHR_external_semaphore_win32 extension commands
#ifdef VK_USE_PLATFORM_WIN32_KHR
    table->ImportSemaphoreWin32HandleKHR = (PFN_vkImportSemaphoreWin32HandleKHR)gpa(dev, "vkImportSemaphoreWin32HandleKHR");
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
    table->GetSemaphoreWin32HandleKHR = (PFN_vkGetSemaphoreWin32HandleKHR)gpa(dev, "vkGetSemaphoreWin32HandleKHR");
#endif // VK_USE_PLATFORM_WIN32_KHR

    // ---- VK_KHR_external_semaphore_fd extension commands
    table->ImportSemaphoreFdKHR = (PFN_vkImportSemaphoreFdKHR)gpa(dev, "vkImportSemaphoreFdKHR");
    table->GetSemaphoreFdKHR = (PFN_vkGetSemaphoreFdKHR)gpa(dev, "vkGetSemaphoreFdKHR");

    // ---- VK_KHR_push_descriptor extension commands
    table->CmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)gpa(dev, "vkCmdPushDescriptorSetKHR");

    // ---- VK_KHR_descriptor_update_template extension commands
    table->CreateDescriptorUpdateTemplateKHR = (PFN_vkCreateDescriptorUpdateTemplateKHR)gpa(dev, "vkCreateDescriptorUpdateTemplateKHR");
    table->DestroyDescriptorUpdateTemplateKHR = (PFN_vkDestroyDescriptorUpdateTemplateKHR)gpa(dev, "vkDestroyDescriptorUpdateTemplateKHR");
    table->UpdateDescriptorSetWithTemplateKHR = (PFN_vkUpdateDescriptorSetWithTemplateKHR)gpa(dev, "vkUpdateDescriptorSetWithTemplateKHR");
    table->CmdPushDescriptorSetWithTemplateKHR = (PFN_vkCmdPushDescriptorSetWithTemplateKHR)gpa(dev, "vkCmdPushDescriptorSetWithTemplateKHR");

    // ---- VK_KHR_shared_presentable_image extension commands
    table->GetSwapchainStatusKHR = (PFN_vkGetSwapchainStatusKHR)gpa(dev, "vkGetSwapchainStatusKHR");

    // ---- VK_KHR_external_fence_win32 extension commands
#ifdef VK_USE_PLATFORM_WIN32_KHR
    table->ImportFenceWin32HandleKHR = (PFN_vkImportFenceWin32HandleKHR)gpa(dev, "vkImportFenceWin32HandleKHR");
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
    table->GetFenceWin32HandleKHR = (PFN_vkGetFenceWin32HandleKHR)gpa(dev, "vkGetFenceWin32HandleKHR");
#endif // VK_USE_PLATFORM_WIN32_KHR

    // ---- VK_KHR_external_fence_fd extension commands
    table->ImportFenceFdKHR = (PFN_vkImportFenceFdKHR)gpa(dev, "vkImportFenceFdKHR");
    table->GetFenceFdKHR = (PFN_vkGetFenceFdKHR)gpa(dev, "vkGetFenceFdKHR");

    // ---- VK_KHR_get_memory_requirements2 extension commands
    table->GetImageMemoryRequirements2KHR = (PFN_vkGetImageMemoryRequirements2KHR)gpa(dev, "vkGetImageMemoryRequirements2KHR");
    table->GetBufferMemoryRequirements2KHR = (PFN_vkGetBufferMemoryRequirements2KHR)gpa(dev, "vkGetBufferMemoryRequirements2KHR");
    table->GetImageSparseMemoryRequirements2KHR = (PFN_vkGetImageSparseMemoryRequirements2KHR)gpa(dev, "vkGetImageSparseMemoryRequirements2KHR");

    // ---- VK_KHR_sampler_ycbcr_conversion extension commands
    table->CreateSamplerYcbcrConversionKHR = (PFN_vkCreateSamplerYcbcrConversionKHR)gpa(dev, "vkCreateSamplerYcbcrConversionKHR");
    table->DestroySamplerYcbcrConversionKHR = (PFN_vkDestroySamplerYcbcrConversionKHR)gpa(dev, "vkDestroySamplerYcbcrConversionKHR");

    // ---- VK_KHR_bind_memory2 extension commands
    table->BindBufferMemory2KHR = (PFN_vkBindBufferMemory2KHR)gpa(dev, "vkBindBufferMemory2KHR");
    table->BindImageMemory2KHR = (PFN_vkBindImageMemory2KHR)gpa(dev, "vkBindImageMemory2KHR");

    // ---- VK_EXT_debug_marker extension commands
    table->DebugMarkerSetObjectTagEXT = (PFN_vkDebugMarkerSetObjectTagEXT)gpa(dev, "vkDebugMarkerSetObjectTagEXT");
    table->DebugMarkerSetObjectNameEXT = (PFN_vkDebugMarkerSetObjectNameEXT)gpa(dev, "vkDebugMarkerSetObjectNameEXT");
    table->CmdDebugMarkerBeginEXT = (PFN_vkCmdDebugMarkerBeginEXT)gpa(dev, "vkCmdDebugMarkerBeginEXT");
    table->CmdDebugMarkerEndEXT = (PFN_vkCmdDebugMarkerEndEXT)gpa(dev, "vkCmdDebugMarkerEndEXT");
    table->CmdDebugMarkerInsertEXT = (PFN_vkCmdDebugMarkerInsertEXT)gpa(dev, "vkCmdDebugMarkerInsertEXT");

    // ---- VK_AMD_draw_indirect_count extension commands
    table->CmdDrawIndirectCountAMD = (PFN_vkCmdDrawIndirectCountAMD)gpa(dev, "vkCmdDrawIndirectCountAMD");
    table->CmdDrawIndexedIndirectCountAMD = (PFN_vkCmdDrawIndexedIndirectCountAMD)gpa(dev, "vkCmdDrawIndexedIndirectCountAMD");

    // ---- VK_AMD_shader_info extension commands
    table->GetShaderInfoAMD = (PFN_vkGetShaderInfoAMD)gpa(dev, "vkGetShaderInfoAMD");

    // ---- VK_NV_external_memory_win32 extension commands
#ifdef VK_USE_PLATFORM_WIN32_KHR
    table->GetMemoryWin32HandleNV = (PFN_vkGetMemoryWin32HandleNV)gpa(dev, "vkGetMemoryWin32HandleNV");
#endif // VK_USE_PLATFORM_WIN32_KHR

    // ---- VK_KHX_device_group extension commands
    table->GetDeviceGroupPeerMemoryFeaturesKHX = (PFN_vkGetDeviceGroupPeerMemoryFeaturesKHX)gpa(dev, "vkGetDeviceGroupPeerMemoryFeaturesKHX");
    table->CmdSetDeviceMaskKHX = (PFN_vkCmdSetDeviceMaskKHX)gpa(dev, "vkCmdSetDeviceMaskKHX");
    table->CmdDispatchBaseKHX = (PFN_vkCmdDispatchBaseKHX)gpa(dev, "vkCmdDispatchBaseKHX");
    table->GetDeviceGroupPresentCapabilitiesKHX = (PFN_vkGetDeviceGroupPresentCapabilitiesKHX)gpa(dev, "vkGetDeviceGroupPresentCapabilitiesKHX");
    table->GetDeviceGroupSurfacePresentModesKHX = (PFN_vkGetDeviceGroupSurfacePresentModesKHX)gpa(dev, "vkGetDeviceGroupSurfacePresentModesKHX");
    table->AcquireNextImage2KHX = (PFN_vkAcquireNextImage2KHX)gpa(dev, "vkAcquireNextImage2KHX");

    // ---- VK_NVX_device_generated_commands extension commands
    table->CmdProcessCommandsNVX = (PFN_vkCmdProcessCommandsNVX)gpa(dev, "vkCmdProcessCommandsNVX");
    table->CmdReserveSpaceForCommandsNVX = (PFN_vkCmdReserveSpaceForCommandsNVX)gpa(dev, "vkCmdReserveSpaceForCommandsNVX");
    table->CreateIndirectCommandsLayoutNVX = (PFN_vkCreateIndirectCommandsLayoutNVX)gpa(dev, "vkCreateIndirectCommandsLayoutNVX");
    table->DestroyIndirectCommandsLayoutNVX = (PFN_vkDestroyIndirectCommandsLayoutNVX)gpa(dev, "vkDestroyIndirectCommandsLayoutNVX");
    table->CreateObjectTableNVX = (PFN_vkCreateObjectTableNVX)gpa(dev, "vkCreateObjectTableNVX");
    table->DestroyObjectTableNVX = (PFN_vkDestroyObjectTableNVX)gpa(dev, "vkDestroyObjectTableNVX");
    table->RegisterObjectsNVX = (PFN_vkRegisterObjectsNVX)gpa(dev, "vkRegisterObjectsNVX");
    table->UnregisterObjectsNVX = (PFN_vkUnregisterObjectsNVX)gpa(dev, "vkUnregisterObjectsNVX");

    // ---- VK_NV_clip_space_w_scaling extension commands
    table->CmdSetViewportWScalingNV = (PFN_vkCmdSetViewportWScalingNV)gpa(dev, "vkCmdSetViewportWScalingNV");

    // ---- VK_EXT_display_control extension commands
    table->DisplayPowerControlEXT = (PFN_vkDisplayPowerControlEXT)gpa(dev, "vkDisplayPowerControlEXT");
    table->RegisterDeviceEventEXT = (PFN_vkRegisterDeviceEventEXT)gpa(dev, "vkRegisterDeviceEventEXT");
    table->RegisterDisplayEventEXT = (PFN_vkRegisterDisplayEventEXT)gpa(dev, "vkRegisterDisplayEventEXT");
    table->GetSwapchainCounterEXT = (PFN_vkGetSwapchainCounterEXT)gpa(dev, "vkGetSwapchainCounterEXT");

    // ---- VK_GOOGLE_display_timing extension commands
    table->GetRefreshCycleDurationGOOGLE = (PFN_vkGetRefreshCycleDurationGOOGLE)gpa(dev, "vkGetRefreshCycleDurationGOOGLE");
    table->GetPastPresentationTimingGOOGLE = (PFN_vkGetPastPresentationTimingGOOGLE)gpa(dev, "vkGetPastPresentationTimingGOOGLE");

    // ---- VK_EXT_discard_rectangles extension commands
    table->CmdSetDiscardRectangleEXT = (PFN_vkCmdSetDiscardRectangleEXT)gpa(dev, "vkCmdSetDiscardRectangleEXT");

    // ---- VK_EXT_hdr_metadata extension commands
    table->SetHdrMetadataEXT = (PFN_vkSetHdrMetadataEXT)gpa(dev, "vkSetHdrMetadataEXT");

    // ---- VK_EXT_sample_locations extension commands
    table->CmdSetSampleLocationsEXT = (PFN_vkCmdSetSampleLocationsEXT)gpa(dev, "vkCmdSetSampleLocationsEXT");

    // ---- VK_EXT_validation_cache extension commands
    table->CreateValidationCacheEXT = (PFN_vkCreateValidationCacheEXT)gpa(dev, "vkCreateValidationCacheEXT");
    table->DestroyValidationCacheEXT = (PFN_vkDestroyValidationCacheEXT)gpa(dev, "vkDestroyValidationCacheEXT");
    table->MergeValidationCachesEXT = (PFN_vkMergeValidationCachesEXT)gpa(dev, "vkMergeValidationCachesEXT");
    table->GetValidationCacheDataEXT = (PFN_vkGetValidationCacheDataEXT)gpa(dev, "vkGetValidationCacheDataEXT");

    // ---- VK_EXT_external_memory_host extension commands
    table->GetMemoryHostPointerPropertiesEXT = (PFN_vkGetMemoryHostPointerPropertiesEXT)gpa(dev, "vkGetMemoryHostPointerPropertiesEXT");
}

// Init Instance function pointer dispatch table with core commands
VKAPI_ATTR void VKAPI_CALL loader_init_instance_core_dispatch_table(VkLayerInstanceDispatchTable *table, PFN_vkGetInstanceProcAddr gpa,
                                                                    VkInstance inst) {

    // ---- Core 1_0 commands
    table->DestroyInstance = (PFN_vkDestroyInstance)gpa(inst, "vkDestroyInstance");
    table->EnumeratePhysicalDevices = (PFN_vkEnumeratePhysicalDevices)gpa(inst, "vkEnumeratePhysicalDevices");
    table->GetPhysicalDeviceFeatures = (PFN_vkGetPhysicalDeviceFeatures)gpa(inst, "vkGetPhysicalDeviceFeatures");
    table->GetPhysicalDeviceFormatProperties = (PFN_vkGetPhysicalDeviceFormatProperties)gpa(inst, "vkGetPhysicalDeviceFormatProperties");
    table->GetPhysicalDeviceImageFormatProperties = (PFN_vkGetPhysicalDeviceImageFormatProperties)gpa(inst, "vkGetPhysicalDeviceImageFormatProperties");
    table->GetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)gpa(inst, "vkGetPhysicalDeviceProperties");
    table->GetPhysicalDeviceQueueFamilyProperties = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gpa(inst, "vkGetPhysicalDeviceQueueFamilyProperties");
    table->GetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)gpa(inst, "vkGetPhysicalDeviceMemoryProperties");
    table->GetInstanceProcAddr = gpa;
    table->EnumerateDeviceExtensionProperties = (PFN_vkEnumerateDeviceExtensionProperties)gpa(inst, "vkEnumerateDeviceExtensionProperties");
    table->EnumerateDeviceLayerProperties = (PFN_vkEnumerateDeviceLayerProperties)gpa(inst, "vkEnumerateDeviceLayerProperties");
    table->GetPhysicalDeviceSparseImageFormatProperties = (PFN_vkGetPhysicalDeviceSparseImageFormatProperties)gpa(inst, "vkGetPhysicalDeviceSparseImageFormatProperties");
}

// Init Instance function pointer dispatch table with core commands
VKAPI_ATTR void VKAPI_CALL loader_init_instance_extension_dispatch_table(VkLayerInstanceDispatchTable *table, PFN_vkGetInstanceProcAddr gpa,
                                                                        VkInstance inst) {

    // ---- VK_KHR_surface extension commands
    table->DestroySurfaceKHR = (PFN_vkDestroySurfaceKHR)gpa(inst, "vkDestroySurfaceKHR");
    table->GetPhysicalDeviceSurfaceSupportKHR = (PFN_vkGetPhysicalDeviceSurfaceSupportKHR)gpa(inst, "vkGetPhysicalDeviceSurfaceSupportKHR");
    table->GetPhysicalDeviceSurfaceCapabilitiesKHR = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)gpa(inst, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    table->GetPhysicalDeviceSurfaceFormatsKHR = (PFN_vkGetPhysicalDeviceSurfaceFormatsKHR)gpa(inst, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    table->GetPhysicalDeviceSurfacePresentModesKHR = (PFN_vkGetPhysicalDeviceSurfacePresentModesKHR)gpa(inst, "vkGetPhysicalDeviceSurfacePresentModesKHR");

    // ---- VK_KHR_display extension commands
    table->GetPhysicalDeviceDisplayPropertiesKHR = (PFN_vkGetPhysicalDeviceDisplayPropertiesKHR)gpa(inst, "vkGetPhysicalDeviceDisplayPropertiesKHR");
    table->GetPhysicalDeviceDisplayPlanePropertiesKHR = (PFN_vkGetPhysicalDeviceDisplayPlanePropertiesKHR)gpa(inst, "vkGetPhysicalDeviceDisplayPlanePropertiesKHR");
    table->GetDisplayPlaneSupportedDisplaysKHR = (PFN_vkGetDisplayPlaneSupportedDisplaysKHR)gpa(inst, "vkGetDisplayPlaneSupportedDisplaysKHR");
    table->GetDisplayModePropertiesKHR = (PFN_vkGetDisplayModePropertiesKHR)gpa(inst, "vkGetDisplayModePropertiesKHR");
    table->CreateDisplayModeKHR = (PFN_vkCreateDisplayModeKHR)gpa(inst, "vkCreateDisplayModeKHR");
    table->GetDisplayPlaneCapabilitiesKHR = (PFN_vkGetDisplayPlaneCapabilitiesKHR)gpa(inst, "vkGetDisplayPlaneCapabilitiesKHR");
    table->CreateDisplayPlaneSurfaceKHR = (PFN_vkCreateDisplayPlaneSurfaceKHR)gpa(inst, "vkCreateDisplayPlaneSurfaceKHR");

    // ---- VK_KHR_xlib_surface extension commands
#ifdef VK_USE_PLATFORM_XLIB_KHR
    table->CreateXlibSurfaceKHR = (PFN_vkCreateXlibSurfaceKHR)gpa(inst, "vkCreateXlibSurfaceKHR");
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    table->GetPhysicalDeviceXlibPresentationSupportKHR = (PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR)gpa(inst, "vkGetPhysicalDeviceXlibPresentationSupportKHR");
#endif // VK_USE_PLATFORM_XLIB_KHR

    // ---- VK_KHR_xcb_surface extension commands
#ifdef VK_USE_PLATFORM_XCB_KHR
    table->CreateXcbSurfaceKHR = (PFN_vkCreateXcbSurfaceKHR)gpa(inst, "vkCreateXcbSurfaceKHR");
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    table->GetPhysicalDeviceXcbPresentationSupportKHR = (PFN_vkGetPhysicalDeviceXcbPresentationSupportKHR)gpa(inst, "vkGetPhysicalDeviceXcbPresentationSupportKHR");
#endif // VK_USE_PLATFORM_XCB_KHR

    // ---- VK_KHR_wayland_surface extension commands
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    table->CreateWaylandSurfaceKHR = (PFN_vkCreateWaylandSurfaceKHR)gpa(inst, "vkCreateWaylandSurfaceKHR");
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    table->GetPhysicalDeviceWaylandPresentationSupportKHR = (PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR)gpa(inst, "vkGetPhysicalDeviceWaylandPresentationSupportKHR");
#endif // VK_USE_PLATFORM_WAYLAND_KHR

    // ---- VK_KHR_mir_surface extension commands
#ifdef VK_USE_PLATFORM_MIR_KHR
    table->CreateMirSurfaceKHR = (PFN_vkCreateMirSurfaceKHR)gpa(inst, "vkCreateMirSurfaceKHR");
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
    table->GetPhysicalDeviceMirPresentationSupportKHR = (PFN_vkGetPhysicalDeviceMirPresentationSupportKHR)gpa(inst, "vkGetPhysicalDeviceMirPresentationSupportKHR");
#endif // VK_USE_PLATFORM_MIR_KHR

    // ---- VK_KHR_android_surface extension commands
#ifdef VK_USE_PLATFORM_ANDROID_KHR
    table->CreateAndroidSurfaceKHR = (PFN_vkCreateAndroidSurfaceKHR)gpa(inst, "vkCreateAndroidSurfaceKHR");
#endif // VK_USE_PLATFORM_ANDROID_KHR

    // ---- VK_KHR_win32_surface extension commands
#ifdef VK_USE_PLATFORM_WIN32_KHR
    table->CreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR)gpa(inst, "vkCreateWin32SurfaceKHR");
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
    table->GetPhysicalDeviceWin32PresentationSupportKHR = (PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR)gpa(inst, "vkGetPhysicalDeviceWin32PresentationSupportKHR");
#endif // VK_USE_PLATFORM_WIN32_KHR

    // ---- VK_KHR_get_physical_device_properties2 extension commands
    table->GetPhysicalDeviceFeatures2KHR = (PFN_vkGetPhysicalDeviceFeatures2KHR)gpa(inst, "vkGetPhysicalDeviceFeatures2KHR");
    table->GetPhysicalDeviceProperties2KHR = (PFN_vkGetPhysicalDeviceProperties2KHR)gpa(inst, "vkGetPhysicalDeviceProperties2KHR");
    table->GetPhysicalDeviceFormatProperties2KHR = (PFN_vkGetPhysicalDeviceFormatProperties2KHR)gpa(inst, "vkGetPhysicalDeviceFormatProperties2KHR");
    table->GetPhysicalDeviceImageFormatProperties2KHR = (PFN_vkGetPhysicalDeviceImageFormatProperties2KHR)gpa(inst, "vkGetPhysicalDeviceImageFormatProperties2KHR");
    table->GetPhysicalDeviceQueueFamilyProperties2KHR = (PFN_vkGetPhysicalDeviceQueueFamilyProperties2KHR)gpa(inst, "vkGetPhysicalDeviceQueueFamilyProperties2KHR");
    table->GetPhysicalDeviceMemoryProperties2KHR = (PFN_vkGetPhysicalDeviceMemoryProperties2KHR)gpa(inst, "vkGetPhysicalDeviceMemoryProperties2KHR");
    table->GetPhysicalDeviceSparseImageFormatProperties2KHR = (PFN_vkGetPhysicalDeviceSparseImageFormatProperties2KHR)gpa(inst, "vkGetPhysicalDeviceSparseImageFormatProperties2KHR");

    // ---- VK_KHR_external_memory_capabilities extension commands
    table->GetPhysicalDeviceExternalBufferPropertiesKHR = (PFN_vkGetPhysicalDeviceExternalBufferPropertiesKHR)gpa(inst, "vkGetPhysicalDeviceExternalBufferPropertiesKHR");

    // ---- VK_KHR_external_semaphore_capabilities extension commands
    table->GetPhysicalDeviceExternalSemaphorePropertiesKHR = (PFN_vkGetPhysicalDeviceExternalSemaphorePropertiesKHR)gpa(inst, "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR");

    // ---- VK_KHR_external_fence_capabilities extension commands
    table->GetPhysicalDeviceExternalFencePropertiesKHR = (PFN_vkGetPhysicalDeviceExternalFencePropertiesKHR)gpa(inst, "vkGetPhysicalDeviceExternalFencePropertiesKHR");

    // ---- VK_KHR_get_surface_capabilities2 extension commands
    table->GetPhysicalDeviceSurfaceCapabilities2KHR = (PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR)gpa(inst, "vkGetPhysicalDeviceSurfaceCapabilities2KHR");
    table->GetPhysicalDeviceSurfaceFormats2KHR = (PFN_vkGetPhysicalDeviceSurfaceFormats2KHR)gpa(inst, "vkGetPhysicalDeviceSurfaceFormats2KHR");

    // ---- VK_EXT_debug_report extension commands
    table->CreateDebugReportCallbackEXT = (PFN_vkCreateDebugReportCallbackEXT)gpa(inst, "vkCreateDebugReportCallbackEXT");
    table->DestroyDebugReportCallbackEXT = (PFN_vkDestroyDebugReportCallbackEXT)gpa(inst, "vkDestroyDebugReportCallbackEXT");
    table->DebugReportMessageEXT = (PFN_vkDebugReportMessageEXT)gpa(inst, "vkDebugReportMessageEXT");

    // ---- VK_NV_external_memory_capabilities extension commands
    table->GetPhysicalDeviceExternalImageFormatPropertiesNV = (PFN_vkGetPhysicalDeviceExternalImageFormatPropertiesNV)gpa(inst, "vkGetPhysicalDeviceExternalImageFormatPropertiesNV");

    // ---- VK_KHX_device_group extension commands
    table->GetPhysicalDevicePresentRectanglesKHX = (PFN_vkGetPhysicalDevicePresentRectanglesKHX)gpa(inst, "vkGetPhysicalDevicePresentRectanglesKHX");

    // ---- VK_NN_vi_surface extension commands
#ifdef VK_USE_PLATFORM_VI_NN
    table->CreateViSurfaceNN = (PFN_vkCreateViSurfaceNN)gpa(inst, "vkCreateViSurfaceNN");
#endif // VK_USE_PLATFORM_VI_NN

    // ---- VK_KHX_device_group_creation extension commands
    table->EnumeratePhysicalDeviceGroupsKHX = (PFN_vkEnumeratePhysicalDeviceGroupsKHX)gpa(inst, "vkEnumeratePhysicalDeviceGroupsKHX");

    // ---- VK_NVX_device_generated_commands extension commands
    table->GetPhysicalDeviceGeneratedCommandsPropertiesNVX = (PFN_vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX)gpa(inst, "vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX");

    // ---- VK_EXT_direct_mode_display extension commands
    table->ReleaseDisplayEXT = (PFN_vkReleaseDisplayEXT)gpa(inst, "vkReleaseDisplayEXT");

    // ---- VK_EXT_acquire_xlib_display extension commands
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    table->AcquireXlibDisplayEXT = (PFN_vkAcquireXlibDisplayEXT)gpa(inst, "vkAcquireXlibDisplayEXT");
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    table->GetRandROutputDisplayEXT = (PFN_vkGetRandROutputDisplayEXT)gpa(inst, "vkGetRandROutputDisplayEXT");
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT

    // ---- VK_EXT_display_surface_counter extension commands
    table->GetPhysicalDeviceSurfaceCapabilities2EXT = (PFN_vkGetPhysicalDeviceSurfaceCapabilities2EXT)gpa(inst, "vkGetPhysicalDeviceSurfaceCapabilities2EXT");

    // ---- VK_MVK_ios_surface extension commands
#ifdef VK_USE_PLATFORM_IOS_MVK
    table->CreateIOSSurfaceMVK = (PFN_vkCreateIOSSurfaceMVK)gpa(inst, "vkCreateIOSSurfaceMVK");
#endif // VK_USE_PLATFORM_IOS_MVK

    // ---- VK_MVK_macos_surface extension commands
#ifdef VK_USE_PLATFORM_MACOS_MVK
    table->CreateMacOSSurfaceMVK = (PFN_vkCreateMacOSSurfaceMVK)gpa(inst, "vkCreateMacOSSurfaceMVK");
#endif // VK_USE_PLATFORM_MACOS_MVK

    // ---- VK_EXT_sample_locations extension commands
    table->GetPhysicalDeviceMultisamplePropertiesEXT = (PFN_vkGetPhysicalDeviceMultisamplePropertiesEXT)gpa(inst, "vkGetPhysicalDeviceMultisamplePropertiesEXT");
}

// Device command lookup function
VKAPI_ATTR void* VKAPI_CALL loader_lookup_device_dispatch_table(const VkLayerDispatchTable *table, const char *name) {
    if (!name || name[0] != 'v' || name[1] != 'k') return NULL;

    name += 2;

    // ---- Core 1_0 commands
    if (!strcmp(name, "GetDeviceProcAddr")) return (void *)table->GetDeviceProcAddr;
    if (!strcmp(name, "DestroyDevice")) return (void *)table->DestroyDevice;
    if (!strcmp(name, "GetDeviceQueue")) return (void *)table->GetDeviceQueue;
    if (!strcmp(name, "QueueSubmit")) return (void *)table->QueueSubmit;
    if (!strcmp(name, "QueueWaitIdle")) return (void *)table->QueueWaitIdle;
    if (!strcmp(name, "DeviceWaitIdle")) return (void *)table->DeviceWaitIdle;
    if (!strcmp(name, "AllocateMemory")) return (void *)table->AllocateMemory;
    if (!strcmp(name, "FreeMemory")) return (void *)table->FreeMemory;
    if (!strcmp(name, "MapMemory")) return (void *)table->MapMemory;
    if (!strcmp(name, "UnmapMemory")) return (void *)table->UnmapMemory;
    if (!strcmp(name, "FlushMappedMemoryRanges")) return (void *)table->FlushMappedMemoryRanges;
    if (!strcmp(name, "InvalidateMappedMemoryRanges")) return (void *)table->InvalidateMappedMemoryRanges;
    if (!strcmp(name, "GetDeviceMemoryCommitment")) return (void *)table->GetDeviceMemoryCommitment;
    if (!strcmp(name, "BindBufferMemory")) return (void *)table->BindBufferMemory;
    if (!strcmp(name, "BindImageMemory")) return (void *)table->BindImageMemory;
    if (!strcmp(name, "GetBufferMemoryRequirements")) return (void *)table->GetBufferMemoryRequirements;
    if (!strcmp(name, "GetImageMemoryRequirements")) return (void *)table->GetImageMemoryRequirements;
    if (!strcmp(name, "GetImageSparseMemoryRequirements")) return (void *)table->GetImageSparseMemoryRequirements;
    if (!strcmp(name, "QueueBindSparse")) return (void *)table->QueueBindSparse;
    if (!strcmp(name, "CreateFence")) return (void *)table->CreateFence;
    if (!strcmp(name, "DestroyFence")) return (void *)table->DestroyFence;
    if (!strcmp(name, "ResetFences")) return (void *)table->ResetFences;
    if (!strcmp(name, "GetFenceStatus")) return (void *)table->GetFenceStatus;
    if (!strcmp(name, "WaitForFences")) return (void *)table->WaitForFences;
    if (!strcmp(name, "CreateSemaphore")) return (void *)table->CreateSemaphore;
    if (!strcmp(name, "DestroySemaphore")) return (void *)table->DestroySemaphore;
    if (!strcmp(name, "CreateEvent")) return (void *)table->CreateEvent;
    if (!strcmp(name, "DestroyEvent")) return (void *)table->DestroyEvent;
    if (!strcmp(name, "GetEventStatus")) return (void *)table->GetEventStatus;
    if (!strcmp(name, "SetEvent")) return (void *)table->SetEvent;
    if (!strcmp(name, "ResetEvent")) return (void *)table->ResetEvent;
    if (!strcmp(name, "CreateQueryPool")) return (void *)table->CreateQueryPool;
    if (!strcmp(name, "DestroyQueryPool")) return (void *)table->DestroyQueryPool;
    if (!strcmp(name, "GetQueryPoolResults")) return (void *)table->GetQueryPoolResults;
    if (!strcmp(name, "CreateBuffer")) return (void *)table->CreateBuffer;
    if (!strcmp(name, "DestroyBuffer")) return (void *)table->DestroyBuffer;
    if (!strcmp(name, "CreateBufferView")) return (void *)table->CreateBufferView;
    if (!strcmp(name, "DestroyBufferView")) return (void *)table->DestroyBufferView;
    if (!strcmp(name, "CreateImage")) return (void *)table->CreateImage;
    if (!strcmp(name, "DestroyImage")) return (void *)table->DestroyImage;
    if (!strcmp(name, "GetImageSubresourceLayout")) return (void *)table->GetImageSubresourceLayout;
    if (!strcmp(name, "CreateImageView")) return (void *)table->CreateImageView;
    if (!strcmp(name, "DestroyImageView")) return (void *)table->DestroyImageView;
    if (!strcmp(name, "CreateShaderModule")) return (void *)table->CreateShaderModule;
    if (!strcmp(name, "DestroyShaderModule")) return (void *)table->DestroyShaderModule;
    if (!strcmp(name, "CreatePipelineCache")) return (void *)table->CreatePipelineCache;
    if (!strcmp(name, "DestroyPipelineCache")) return (void *)table->DestroyPipelineCache;
    if (!strcmp(name, "GetPipelineCacheData")) return (void *)table->GetPipelineCacheData;
    if (!strcmp(name, "MergePipelineCaches")) return (void *)table->MergePipelineCaches;
    if (!strcmp(name, "CreateGraphicsPipelines")) return (void *)table->CreateGraphicsPipelines;
    if (!strcmp(name, "CreateComputePipelines")) return (void *)table->CreateComputePipelines;
    if (!strcmp(name, "DestroyPipeline")) return (void *)table->DestroyPipeline;
    if (!strcmp(name, "CreatePipelineLayout")) return (void *)table->CreatePipelineLayout;
    if (!strcmp(name, "DestroyPipelineLayout")) return (void *)table->DestroyPipelineLayout;
    if (!strcmp(name, "CreateSampler")) return (void *)table->CreateSampler;
    if (!strcmp(name, "DestroySampler")) return (void *)table->DestroySampler;
    if (!strcmp(name, "CreateDescriptorSetLayout")) return (void *)table->CreateDescriptorSetLayout;
    if (!strcmp(name, "DestroyDescriptorSetLayout")) return (void *)table->DestroyDescriptorSetLayout;
    if (!strcmp(name, "CreateDescriptorPool")) return (void *)table->CreateDescriptorPool;
    if (!strcmp(name, "DestroyDescriptorPool")) return (void *)table->DestroyDescriptorPool;
    if (!strcmp(name, "ResetDescriptorPool")) return (void *)table->ResetDescriptorPool;
    if (!strcmp(name, "AllocateDescriptorSets")) return (void *)table->AllocateDescriptorSets;
    if (!strcmp(name, "FreeDescriptorSets")) return (void *)table->FreeDescriptorSets;
    if (!strcmp(name, "UpdateDescriptorSets")) return (void *)table->UpdateDescriptorSets;
    if (!strcmp(name, "CreateFramebuffer")) return (void *)table->CreateFramebuffer;
    if (!strcmp(name, "DestroyFramebuffer")) return (void *)table->DestroyFramebuffer;
    if (!strcmp(name, "CreateRenderPass")) return (void *)table->CreateRenderPass;
    if (!strcmp(name, "DestroyRenderPass")) return (void *)table->DestroyRenderPass;
    if (!strcmp(name, "GetRenderAreaGranularity")) return (void *)table->GetRenderAreaGranularity;
    if (!strcmp(name, "CreateCommandPool")) return (void *)table->CreateCommandPool;
    if (!strcmp(name, "DestroyCommandPool")) return (void *)table->DestroyCommandPool;
    if (!strcmp(name, "ResetCommandPool")) return (void *)table->ResetCommandPool;
    if (!strcmp(name, "AllocateCommandBuffers")) return (void *)table->AllocateCommandBuffers;
    if (!strcmp(name, "FreeCommandBuffers")) return (void *)table->FreeCommandBuffers;
    if (!strcmp(name, "BeginCommandBuffer")) return (void *)table->BeginCommandBuffer;
    if (!strcmp(name, "EndCommandBuffer")) return (void *)table->EndCommandBuffer;
    if (!strcmp(name, "ResetCommandBuffer")) return (void *)table->ResetCommandBuffer;
    if (!strcmp(name, "CmdBindPipeline")) return (void *)table->CmdBindPipeline;
    if (!strcmp(name, "CmdSetViewport")) return (void *)table->CmdSetViewport;
    if (!strcmp(name, "CmdSetScissor")) return (void *)table->CmdSetScissor;
    if (!strcmp(name, "CmdSetLineWidth")) return (void *)table->CmdSetLineWidth;
    if (!strcmp(name, "CmdSetDepthBias")) return (void *)table->CmdSetDepthBias;
    if (!strcmp(name, "CmdSetBlendConstants")) return (void *)table->CmdSetBlendConstants;
    if (!strcmp(name, "CmdSetDepthBounds")) return (void *)table->CmdSetDepthBounds;
    if (!strcmp(name, "CmdSetStencilCompareMask")) return (void *)table->CmdSetStencilCompareMask;
    if (!strcmp(name, "CmdSetStencilWriteMask")) return (void *)table->CmdSetStencilWriteMask;
    if (!strcmp(name, "CmdSetStencilReference")) return (void *)table->CmdSetStencilReference;
    if (!strcmp(name, "CmdBindDescriptorSets")) return (void *)table->CmdBindDescriptorSets;
    if (!strcmp(name, "CmdBindIndexBuffer")) return (void *)table->CmdBindIndexBuffer;
    if (!strcmp(name, "CmdBindVertexBuffers")) return (void *)table->CmdBindVertexBuffers;
    if (!strcmp(name, "CmdDraw")) return (void *)table->CmdDraw;
    if (!strcmp(name, "CmdDrawIndexed")) return (void *)table->CmdDrawIndexed;
    if (!strcmp(name, "CmdDrawIndirect")) return (void *)table->CmdDrawIndirect;
    if (!strcmp(name, "CmdDrawIndexedIndirect")) return (void *)table->CmdDrawIndexedIndirect;
    if (!strcmp(name, "CmdDispatch")) return (void *)table->CmdDispatch;
    if (!strcmp(name, "CmdDispatchIndirect")) return (void *)table->CmdDispatchIndirect;
    if (!strcmp(name, "CmdCopyBuffer")) return (void *)table->CmdCopyBuffer;
    if (!strcmp(name, "CmdCopyImage")) return (void *)table->CmdCopyImage;
    if (!strcmp(name, "CmdBlitImage")) return (void *)table->CmdBlitImage;
    if (!strcmp(name, "CmdCopyBufferToImage")) return (void *)table->CmdCopyBufferToImage;
    if (!strcmp(name, "CmdCopyImageToBuffer")) return (void *)table->CmdCopyImageToBuffer;
    if (!strcmp(name, "CmdUpdateBuffer")) return (void *)table->CmdUpdateBuffer;
    if (!strcmp(name, "CmdFillBuffer")) return (void *)table->CmdFillBuffer;
    if (!strcmp(name, "CmdClearColorImage")) return (void *)table->CmdClearColorImage;
    if (!strcmp(name, "CmdClearDepthStencilImage")) return (void *)table->CmdClearDepthStencilImage;
    if (!strcmp(name, "CmdClearAttachments")) return (void *)table->CmdClearAttachments;
    if (!strcmp(name, "CmdResolveImage")) return (void *)table->CmdResolveImage;
    if (!strcmp(name, "CmdSetEvent")) return (void *)table->CmdSetEvent;
    if (!strcmp(name, "CmdResetEvent")) return (void *)table->CmdResetEvent;
    if (!strcmp(name, "CmdWaitEvents")) return (void *)table->CmdWaitEvents;
    if (!strcmp(name, "CmdPipelineBarrier")) return (void *)table->CmdPipelineBarrier;
    if (!strcmp(name, "CmdBeginQuery")) return (void *)table->CmdBeginQuery;
    if (!strcmp(name, "CmdEndQuery")) return (void *)table->CmdEndQuery;
    if (!strcmp(name, "CmdResetQueryPool")) return (void *)table->CmdResetQueryPool;
    if (!strcmp(name, "CmdWriteTimestamp")) return (void *)table->CmdWriteTimestamp;
    if (!strcmp(name, "CmdCopyQueryPoolResults")) return (void *)table->CmdCopyQueryPoolResults;
    if (!strcmp(name, "CmdPushConstants")) return (void *)table->CmdPushConstants;
    if (!strcmp(name, "CmdBeginRenderPass")) return (void *)table->CmdBeginRenderPass;
    if (!strcmp(name, "CmdNextSubpass")) return (void *)table->CmdNextSubpass;
    if (!strcmp(name, "CmdEndRenderPass")) return (void *)table->CmdEndRenderPass;
    if (!strcmp(name, "CmdExecuteCommands")) return (void *)table->CmdExecuteCommands;

    // ---- VK_KHR_swapchain extension commands
    if (!strcmp(name, "CreateSwapchainKHR")) return (void *)table->CreateSwapchainKHR;
    if (!strcmp(name, "DestroySwapchainKHR")) return (void *)table->DestroySwapchainKHR;
    if (!strcmp(name, "GetSwapchainImagesKHR")) return (void *)table->GetSwapchainImagesKHR;
    if (!strcmp(name, "AcquireNextImageKHR")) return (void *)table->AcquireNextImageKHR;
    if (!strcmp(name, "QueuePresentKHR")) return (void *)table->QueuePresentKHR;

    // ---- VK_KHR_display_swapchain extension commands
    if (!strcmp(name, "CreateSharedSwapchainsKHR")) return (void *)table->CreateSharedSwapchainsKHR;

    // ---- VK_KHR_maintenance1 extension commands
    if (!strcmp(name, "TrimCommandPoolKHR")) return (void *)table->TrimCommandPoolKHR;

    // ---- VK_KHR_external_memory_win32 extension commands
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp(name, "GetMemoryWin32HandleKHR")) return (void *)table->GetMemoryWin32HandleKHR;
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp(name, "GetMemoryWin32HandlePropertiesKHR")) return (void *)table->GetMemoryWin32HandlePropertiesKHR;
#endif // VK_USE_PLATFORM_WIN32_KHR

    // ---- VK_KHR_external_memory_fd extension commands
    if (!strcmp(name, "GetMemoryFdKHR")) return (void *)table->GetMemoryFdKHR;
    if (!strcmp(name, "GetMemoryFdPropertiesKHR")) return (void *)table->GetMemoryFdPropertiesKHR;

    // ---- VK_KHR_external_semaphore_win32 extension commands
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp(name, "ImportSemaphoreWin32HandleKHR")) return (void *)table->ImportSemaphoreWin32HandleKHR;
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp(name, "GetSemaphoreWin32HandleKHR")) return (void *)table->GetSemaphoreWin32HandleKHR;
#endif // VK_USE_PLATFORM_WIN32_KHR

    // ---- VK_KHR_external_semaphore_fd extension commands
    if (!strcmp(name, "ImportSemaphoreFdKHR")) return (void *)table->ImportSemaphoreFdKHR;
    if (!strcmp(name, "GetSemaphoreFdKHR")) return (void *)table->GetSemaphoreFdKHR;

    // ---- VK_KHR_push_descriptor extension commands
    if (!strcmp(name, "CmdPushDescriptorSetKHR")) return (void *)table->CmdPushDescriptorSetKHR;

    // ---- VK_KHR_descriptor_update_template extension commands
    if (!strcmp(name, "CreateDescriptorUpdateTemplateKHR")) return (void *)table->CreateDescriptorUpdateTemplateKHR;
    if (!strcmp(name, "DestroyDescriptorUpdateTemplateKHR")) return (void *)table->DestroyDescriptorUpdateTemplateKHR;
    if (!strcmp(name, "UpdateDescriptorSetWithTemplateKHR")) return (void *)table->UpdateDescriptorSetWithTemplateKHR;
    if (!strcmp(name, "CmdPushDescriptorSetWithTemplateKHR")) return (void *)table->CmdPushDescriptorSetWithTemplateKHR;

    // ---- VK_KHR_shared_presentable_image extension commands
    if (!strcmp(name, "GetSwapchainStatusKHR")) return (void *)table->GetSwapchainStatusKHR;

    // ---- VK_KHR_external_fence_win32 extension commands
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp(name, "ImportFenceWin32HandleKHR")) return (void *)table->ImportFenceWin32HandleKHR;
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp(name, "GetFenceWin32HandleKHR")) return (void *)table->GetFenceWin32HandleKHR;
#endif // VK_USE_PLATFORM_WIN32_KHR

    // ---- VK_KHR_external_fence_fd extension commands
    if (!strcmp(name, "ImportFenceFdKHR")) return (void *)table->ImportFenceFdKHR;
    if (!strcmp(name, "GetFenceFdKHR")) return (void *)table->GetFenceFdKHR;

    // ---- VK_KHR_get_memory_requirements2 extension commands
    if (!strcmp(name, "GetImageMemoryRequirements2KHR")) return (void *)table->GetImageMemoryRequirements2KHR;
    if (!strcmp(name, "GetBufferMemoryRequirements2KHR")) return (void *)table->GetBufferMemoryRequirements2KHR;
    if (!strcmp(name, "GetImageSparseMemoryRequirements2KHR")) return (void *)table->GetImageSparseMemoryRequirements2KHR;

    // ---- VK_KHR_sampler_ycbcr_conversion extension commands
    if (!strcmp(name, "CreateSamplerYcbcrConversionKHR")) return (void *)table->CreateSamplerYcbcrConversionKHR;
    if (!strcmp(name, "DestroySamplerYcbcrConversionKHR")) return (void *)table->DestroySamplerYcbcrConversionKHR;

    // ---- VK_KHR_bind_memory2 extension commands
    if (!strcmp(name, "BindBufferMemory2KHR")) return (void *)table->BindBufferMemory2KHR;
    if (!strcmp(name, "BindImageMemory2KHR")) return (void *)table->BindImageMemory2KHR;

    // ---- VK_EXT_debug_marker extension commands
    if (!strcmp(name, "DebugMarkerSetObjectTagEXT")) return (void *)table->DebugMarkerSetObjectTagEXT;
    if (!strcmp(name, "DebugMarkerSetObjectNameEXT")) return (void *)table->DebugMarkerSetObjectNameEXT;
    if (!strcmp(name, "CmdDebugMarkerBeginEXT")) return (void *)table->CmdDebugMarkerBeginEXT;
    if (!strcmp(name, "CmdDebugMarkerEndEXT")) return (void *)table->CmdDebugMarkerEndEXT;
    if (!strcmp(name, "CmdDebugMarkerInsertEXT")) return (void *)table->CmdDebugMarkerInsertEXT;

    // ---- VK_AMD_draw_indirect_count extension commands
    if (!strcmp(name, "CmdDrawIndirectCountAMD")) return (void *)table->CmdDrawIndirectCountAMD;
    if (!strcmp(name, "CmdDrawIndexedIndirectCountAMD")) return (void *)table->CmdDrawIndexedIndirectCountAMD;

    // ---- VK_AMD_shader_info extension commands
    if (!strcmp(name, "GetShaderInfoAMD")) return (void *)table->GetShaderInfoAMD;

    // ---- VK_NV_external_memory_win32 extension commands
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp(name, "GetMemoryWin32HandleNV")) return (void *)table->GetMemoryWin32HandleNV;
#endif // VK_USE_PLATFORM_WIN32_KHR

    // ---- VK_KHX_device_group extension commands
    if (!strcmp(name, "GetDeviceGroupPeerMemoryFeaturesKHX")) return (void *)table->GetDeviceGroupPeerMemoryFeaturesKHX;
    if (!strcmp(name, "CmdSetDeviceMaskKHX")) return (void *)table->CmdSetDeviceMaskKHX;
    if (!strcmp(name, "CmdDispatchBaseKHX")) return (void *)table->CmdDispatchBaseKHX;
    if (!strcmp(name, "GetDeviceGroupPresentCapabilitiesKHX")) return (void *)table->GetDeviceGroupPresentCapabilitiesKHX;
    if (!strcmp(name, "GetDeviceGroupSurfacePresentModesKHX")) return (void *)table->GetDeviceGroupSurfacePresentModesKHX;
    if (!strcmp(name, "AcquireNextImage2KHX")) return (void *)table->AcquireNextImage2KHX;

    // ---- VK_NVX_device_generated_commands extension commands
    if (!strcmp(name, "CmdProcessCommandsNVX")) return (void *)table->CmdProcessCommandsNVX;
    if (!strcmp(name, "CmdReserveSpaceForCommandsNVX")) return (void *)table->CmdReserveSpaceForCommandsNVX;
    if (!strcmp(name, "CreateIndirectCommandsLayoutNVX")) return (void *)table->CreateIndirectCommandsLayoutNVX;
    if (!strcmp(name, "DestroyIndirectCommandsLayoutNVX")) return (void *)table->DestroyIndirectCommandsLayoutNVX;
    if (!strcmp(name, "CreateObjectTableNVX")) return (void *)table->CreateObjectTableNVX;
    if (!strcmp(name, "DestroyObjectTableNVX")) return (void *)table->DestroyObjectTableNVX;
    if (!strcmp(name, "RegisterObjectsNVX")) return (void *)table->RegisterObjectsNVX;
    if (!strcmp(name, "UnregisterObjectsNVX")) return (void *)table->UnregisterObjectsNVX;

    // ---- VK_NV_clip_space_w_scaling extension commands
    if (!strcmp(name, "CmdSetViewportWScalingNV")) return (void *)table->CmdSetViewportWScalingNV;

    // ---- VK_EXT_display_control extension commands
    if (!strcmp(name, "DisplayPowerControlEXT")) return (void *)table->DisplayPowerControlEXT;
    if (!strcmp(name, "RegisterDeviceEventEXT")) return (void *)table->RegisterDeviceEventEXT;
    if (!strcmp(name, "RegisterDisplayEventEXT")) return (void *)table->RegisterDisplayEventEXT;
    if (!strcmp(name, "GetSwapchainCounterEXT")) return (void *)table->GetSwapchainCounterEXT;

    // ---- VK_GOOGLE_display_timing extension commands
    if (!strcmp(name, "GetRefreshCycleDurationGOOGLE")) return (void *)table->GetRefreshCycleDurationGOOGLE;
    if (!strcmp(name, "GetPastPresentationTimingGOOGLE")) return (void *)table->GetPastPresentationTimingGOOGLE;

    // ---- VK_EXT_discard_rectangles extension commands
    if (!strcmp(name, "CmdSetDiscardRectangleEXT")) return (void *)table->CmdSetDiscardRectangleEXT;

    // ---- VK_EXT_hdr_metadata extension commands
    if (!strcmp(name, "SetHdrMetadataEXT")) return (void *)table->SetHdrMetadataEXT;

    // ---- VK_EXT_sample_locations extension commands
    if (!strcmp(name, "CmdSetSampleLocationsEXT")) return (void *)table->CmdSetSampleLocationsEXT;

    // ---- VK_EXT_validation_cache extension commands
    if (!strcmp(name, "CreateValidationCacheEXT")) return (void *)table->CreateValidationCacheEXT;
    if (!strcmp(name, "DestroyValidationCacheEXT")) return (void *)table->DestroyValidationCacheEXT;
    if (!strcmp(name, "MergeValidationCachesEXT")) return (void *)table->MergeValidationCachesEXT;
    if (!strcmp(name, "GetValidationCacheDataEXT")) return (void *)table->GetValidationCacheDataEXT;

    // ---- VK_EXT_external_memory_host extension commands
    if (!strcmp(name, "GetMemoryHostPointerPropertiesEXT")) return (void *)table->GetMemoryHostPointerPropertiesEXT;

    return NULL;
}

// Instance command lookup function
VKAPI_ATTR void* VKAPI_CALL loader_lookup_instance_dispatch_table(const VkLayerInstanceDispatchTable *table, const char *name,
                                                                 bool *found_name) {
    if (!name || name[0] != 'v' || name[1] != 'k') {
        *found_name = false;
        return NULL;
    }

    *found_name = true;
    name += 2;

    // ---- Core 1_0 commands
    if (!strcmp(name, "DestroyInstance")) return (void *)table->DestroyInstance;
    if (!strcmp(name, "EnumeratePhysicalDevices")) return (void *)table->EnumeratePhysicalDevices;
    if (!strcmp(name, "GetPhysicalDeviceFeatures")) return (void *)table->GetPhysicalDeviceFeatures;
    if (!strcmp(name, "GetPhysicalDeviceFormatProperties")) return (void *)table->GetPhysicalDeviceFormatProperties;
    if (!strcmp(name, "GetPhysicalDeviceImageFormatProperties")) return (void *)table->GetPhysicalDeviceImageFormatProperties;
    if (!strcmp(name, "GetPhysicalDeviceProperties")) return (void *)table->GetPhysicalDeviceProperties;
    if (!strcmp(name, "GetPhysicalDeviceQueueFamilyProperties")) return (void *)table->GetPhysicalDeviceQueueFamilyProperties;
    if (!strcmp(name, "GetPhysicalDeviceMemoryProperties")) return (void *)table->GetPhysicalDeviceMemoryProperties;
    if (!strcmp(name, "GetInstanceProcAddr")) return (void *)table->GetInstanceProcAddr;
    if (!strcmp(name, "EnumerateDeviceExtensionProperties")) return (void *)table->EnumerateDeviceExtensionProperties;
    if (!strcmp(name, "EnumerateDeviceLayerProperties")) return (void *)table->EnumerateDeviceLayerProperties;
    if (!strcmp(name, "GetPhysicalDeviceSparseImageFormatProperties")) return (void *)table->GetPhysicalDeviceSparseImageFormatProperties;

    // ---- VK_KHR_surface extension commands
    if (!strcmp(name, "DestroySurfaceKHR")) return (void *)table->DestroySurfaceKHR;
    if (!strcmp(name, "GetPhysicalDeviceSurfaceSupportKHR")) return (void *)table->GetPhysicalDeviceSurfaceSupportKHR;
    if (!strcmp(name, "GetPhysicalDeviceSurfaceCapabilitiesKHR")) return (void *)table->GetPhysicalDeviceSurfaceCapabilitiesKHR;
    if (!strcmp(name, "GetPhysicalDeviceSurfaceFormatsKHR")) return (void *)table->GetPhysicalDeviceSurfaceFormatsKHR;
    if (!strcmp(name, "GetPhysicalDeviceSurfacePresentModesKHR")) return (void *)table->GetPhysicalDeviceSurfacePresentModesKHR;

    // ---- VK_KHR_display extension commands
    if (!strcmp(name, "GetPhysicalDeviceDisplayPropertiesKHR")) return (void *)table->GetPhysicalDeviceDisplayPropertiesKHR;
    if (!strcmp(name, "GetPhysicalDeviceDisplayPlanePropertiesKHR")) return (void *)table->GetPhysicalDeviceDisplayPlanePropertiesKHR;
    if (!strcmp(name, "GetDisplayPlaneSupportedDisplaysKHR")) return (void *)table->GetDisplayPlaneSupportedDisplaysKHR;
    if (!strcmp(name, "GetDisplayModePropertiesKHR")) return (void *)table->GetDisplayModePropertiesKHR;
    if (!strcmp(name, "CreateDisplayModeKHR")) return (void *)table->CreateDisplayModeKHR;
    if (!strcmp(name, "GetDisplayPlaneCapabilitiesKHR")) return (void *)table->GetDisplayPlaneCapabilitiesKHR;
    if (!strcmp(name, "CreateDisplayPlaneSurfaceKHR")) return (void *)table->CreateDisplayPlaneSurfaceKHR;

    // ---- VK_KHR_xlib_surface extension commands
#ifdef VK_USE_PLATFORM_XLIB_KHR
    if (!strcmp(name, "CreateXlibSurfaceKHR")) return (void *)table->CreateXlibSurfaceKHR;
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    if (!strcmp(name, "GetPhysicalDeviceXlibPresentationSupportKHR")) return (void *)table->GetPhysicalDeviceXlibPresentationSupportKHR;
#endif // VK_USE_PLATFORM_XLIB_KHR

    // ---- VK_KHR_xcb_surface extension commands
#ifdef VK_USE_PLATFORM_XCB_KHR
    if (!strcmp(name, "CreateXcbSurfaceKHR")) return (void *)table->CreateXcbSurfaceKHR;
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    if (!strcmp(name, "GetPhysicalDeviceXcbPresentationSupportKHR")) return (void *)table->GetPhysicalDeviceXcbPresentationSupportKHR;
#endif // VK_USE_PLATFORM_XCB_KHR

    // ---- VK_KHR_wayland_surface extension commands
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    if (!strcmp(name, "CreateWaylandSurfaceKHR")) return (void *)table->CreateWaylandSurfaceKHR;
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    if (!strcmp(name, "GetPhysicalDeviceWaylandPresentationSupportKHR")) return (void *)table->GetPhysicalDeviceWaylandPresentationSupportKHR;
#endif // VK_USE_PLATFORM_WAYLAND_KHR

    // ---- VK_KHR_mir_surface extension commands
#ifdef VK_USE_PLATFORM_MIR_KHR
    if (!strcmp(name, "CreateMirSurfaceKHR")) return (void *)table->CreateMirSurfaceKHR;
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
    if (!strcmp(name, "GetPhysicalDeviceMirPresentationSupportKHR")) return (void *)table->GetPhysicalDeviceMirPresentationSupportKHR;
#endif // VK_USE_PLATFORM_MIR_KHR

    // ---- VK_KHR_android_surface extension commands
#ifdef VK_USE_PLATFORM_ANDROID_KHR
    if (!strcmp(name, "CreateAndroidSurfaceKHR")) return (void *)table->CreateAndroidSurfaceKHR;
#endif // VK_USE_PLATFORM_ANDROID_KHR

    // ---- VK_KHR_win32_surface extension commands
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp(name, "CreateWin32SurfaceKHR")) return (void *)table->CreateWin32SurfaceKHR;
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp(name, "GetPhysicalDeviceWin32PresentationSupportKHR")) return (void *)table->GetPhysicalDeviceWin32PresentationSupportKHR;
#endif // VK_USE_PLATFORM_WIN32_KHR

    // ---- VK_KHR_get_physical_device_properties2 extension commands
    if (!strcmp(name, "GetPhysicalDeviceFeatures2KHR")) return (void *)table->GetPhysicalDeviceFeatures2KHR;
    if (!strcmp(name, "GetPhysicalDeviceProperties2KHR")) return (void *)table->GetPhysicalDeviceProperties2KHR;
    if (!strcmp(name, "GetPhysicalDeviceFormatProperties2KHR")) return (void *)table->GetPhysicalDeviceFormatProperties2KHR;
    if (!strcmp(name, "GetPhysicalDeviceImageFormatProperties2KHR")) return (void *)table->GetPhysicalDeviceImageFormatProperties2KHR;
    if (!strcmp(name, "GetPhysicalDeviceQueueFamilyProperties2KHR")) return (void *)table->GetPhysicalDeviceQueueFamilyProperties2KHR;
    if (!strcmp(name, "GetPhysicalDeviceMemoryProperties2KHR")) return (void *)table->GetPhysicalDeviceMemoryProperties2KHR;
    if (!strcmp(name, "GetPhysicalDeviceSparseImageFormatProperties2KHR")) return (void *)table->GetPhysicalDeviceSparseImageFormatProperties2KHR;

    // ---- VK_KHR_external_memory_capabilities extension commands
    if (!strcmp(name, "GetPhysicalDeviceExternalBufferPropertiesKHR")) return (void *)table->GetPhysicalDeviceExternalBufferPropertiesKHR;

    // ---- VK_KHR_external_semaphore_capabilities extension commands
    if (!strcmp(name, "GetPhysicalDeviceExternalSemaphorePropertiesKHR")) return (void *)table->GetPhysicalDeviceExternalSemaphorePropertiesKHR;

    // ---- VK_KHR_external_fence_capabilities extension commands
    if (!strcmp(name, "GetPhysicalDeviceExternalFencePropertiesKHR")) return (void *)table->GetPhysicalDeviceExternalFencePropertiesKHR;

    // ---- VK_KHR_get_surface_capabilities2 extension commands
    if (!strcmp(name, "GetPhysicalDeviceSurfaceCapabilities2KHR")) return (void *)table->GetPhysicalDeviceSurfaceCapabilities2KHR;
    if (!strcmp(name, "GetPhysicalDeviceSurfaceFormats2KHR")) return (void *)table->GetPhysicalDeviceSurfaceFormats2KHR;

    // ---- VK_EXT_debug_report extension commands
    if (!strcmp(name, "CreateDebugReportCallbackEXT")) return (void *)table->CreateDebugReportCallbackEXT;
    if (!strcmp(name, "DestroyDebugReportCallbackEXT")) return (void *)table->DestroyDebugReportCallbackEXT;
    if (!strcmp(name, "DebugReportMessageEXT")) return (void *)table->DebugReportMessageEXT;

    // ---- VK_NV_external_memory_capabilities extension commands
    if (!strcmp(name, "GetPhysicalDeviceExternalImageFormatPropertiesNV")) return (void *)table->GetPhysicalDeviceExternalImageFormatPropertiesNV;

    // ---- VK_KHX_device_group extension commands
    if (!strcmp(name, "GetPhysicalDevicePresentRectanglesKHX")) return (void *)table->GetPhysicalDevicePresentRectanglesKHX;

    // ---- VK_NN_vi_surface extension commands
#ifdef VK_USE_PLATFORM_VI_NN
    if (!strcmp(name, "CreateViSurfaceNN")) return (void *)table->CreateViSurfaceNN;
#endif // VK_USE_PLATFORM_VI_NN

    // ---- VK_KHX_device_group_creation extension commands
    if (!strcmp(name, "EnumeratePhysicalDeviceGroupsKHX")) return (void *)table->EnumeratePhysicalDeviceGroupsKHX;

    // ---- VK_NVX_device_generated_commands extension commands
    if (!strcmp(name, "GetPhysicalDeviceGeneratedCommandsPropertiesNVX")) return (void *)table->GetPhysicalDeviceGeneratedCommandsPropertiesNVX;

    // ---- VK_EXT_direct_mode_display extension commands
    if (!strcmp(name, "ReleaseDisplayEXT")) return (void *)table->ReleaseDisplayEXT;

    // ---- VK_EXT_acquire_xlib_display extension commands
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    if (!strcmp(name, "AcquireXlibDisplayEXT")) return (void *)table->AcquireXlibDisplayEXT;
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    if (!strcmp(name, "GetRandROutputDisplayEXT")) return (void *)table->GetRandROutputDisplayEXT;
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT

    // ---- VK_EXT_display_surface_counter extension commands
    if (!strcmp(name, "GetPhysicalDeviceSurfaceCapabilities2EXT")) return (void *)table->GetPhysicalDeviceSurfaceCapabilities2EXT;

    // ---- VK_MVK_ios_surface extension commands
#ifdef VK_USE_PLATFORM_IOS_MVK
    if (!strcmp(name, "CreateIOSSurfaceMVK")) return (void *)table->CreateIOSSurfaceMVK;
#endif // VK_USE_PLATFORM_IOS_MVK

    // ---- VK_MVK_macos_surface extension commands
#ifdef VK_USE_PLATFORM_MACOS_MVK
    if (!strcmp(name, "CreateMacOSSurfaceMVK")) return (void *)table->CreateMacOSSurfaceMVK;
#endif // VK_USE_PLATFORM_MACOS_MVK

    // ---- VK_EXT_sample_locations extension commands
    if (!strcmp(name, "GetPhysicalDeviceMultisamplePropertiesEXT")) return (void *)table->GetPhysicalDeviceMultisamplePropertiesEXT;

    *found_name = false;
    return NULL;
}


// ---- VK_KHR_maintenance1 extension trampoline/terminators

VKAPI_ATTR void VKAPI_CALL TrimCommandPoolKHR(
    VkDevice                                    device,
    VkCommandPool                               commandPool,
    VkCommandPoolTrimFlagsKHR                   flags) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    disp->TrimCommandPoolKHR(device, commandPool, flags);
}


// ---- VK_KHR_external_memory_win32 extension trampoline/terminators

#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL GetMemoryWin32HandleKHR(
    VkDevice                                    device,
    const VkMemoryGetWin32HandleInfoKHR*        pGetWin32HandleInfo,
    HANDLE*                                     pHandle) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetMemoryWin32HandleKHR(device, pGetWin32HandleInfo, pHandle);
}

#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL GetMemoryWin32HandlePropertiesKHR(
    VkDevice                                    device,
    VkExternalMemoryHandleTypeFlagBitsKHR       handleType,
    HANDLE                                      handle,
    VkMemoryWin32HandlePropertiesKHR*           pMemoryWin32HandleProperties) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetMemoryWin32HandlePropertiesKHR(device, handleType, handle, pMemoryWin32HandleProperties);
}

#endif // VK_USE_PLATFORM_WIN32_KHR

// ---- VK_KHR_external_memory_fd extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL GetMemoryFdKHR(
    VkDevice                                    device,
    const VkMemoryGetFdInfoKHR*                 pGetFdInfo,
    int*                                        pFd) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetMemoryFdKHR(device, pGetFdInfo, pFd);
}

VKAPI_ATTR VkResult VKAPI_CALL GetMemoryFdPropertiesKHR(
    VkDevice                                    device,
    VkExternalMemoryHandleTypeFlagBitsKHR       handleType,
    int                                         fd,
    VkMemoryFdPropertiesKHR*                    pMemoryFdProperties) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetMemoryFdPropertiesKHR(device, handleType, fd, pMemoryFdProperties);
}


// ---- VK_KHR_external_semaphore_win32 extension trampoline/terminators

#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL ImportSemaphoreWin32HandleKHR(
    VkDevice                                    device,
    const VkImportSemaphoreWin32HandleInfoKHR*  pImportSemaphoreWin32HandleInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->ImportSemaphoreWin32HandleKHR(device, pImportSemaphoreWin32HandleInfo);
}

#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL GetSemaphoreWin32HandleKHR(
    VkDevice                                    device,
    const VkSemaphoreGetWin32HandleInfoKHR*     pGetWin32HandleInfo,
    HANDLE*                                     pHandle) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetSemaphoreWin32HandleKHR(device, pGetWin32HandleInfo, pHandle);
}

#endif // VK_USE_PLATFORM_WIN32_KHR

// ---- VK_KHR_external_semaphore_fd extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL ImportSemaphoreFdKHR(
    VkDevice                                    device,
    const VkImportSemaphoreFdInfoKHR*           pImportSemaphoreFdInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->ImportSemaphoreFdKHR(device, pImportSemaphoreFdInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL GetSemaphoreFdKHR(
    VkDevice                                    device,
    const VkSemaphoreGetFdInfoKHR*              pGetFdInfo,
    int*                                        pFd) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetSemaphoreFdKHR(device, pGetFdInfo, pFd);
}


// ---- VK_KHR_push_descriptor extension trampoline/terminators

VKAPI_ATTR void VKAPI_CALL CmdPushDescriptorSetKHR(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            layout,
    uint32_t                                    set,
    uint32_t                                    descriptorWriteCount,
    const VkWriteDescriptorSet*                 pDescriptorWrites) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdPushDescriptorSetKHR(commandBuffer, pipelineBindPoint, layout, set, descriptorWriteCount, pDescriptorWrites);
}


// ---- VK_KHR_descriptor_update_template extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL CreateDescriptorUpdateTemplateKHR(
    VkDevice                                    device,
    const VkDescriptorUpdateTemplateCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorUpdateTemplateKHR*              pDescriptorUpdateTemplate) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->CreateDescriptorUpdateTemplateKHR(device, pCreateInfo, pAllocator, pDescriptorUpdateTemplate);
}

VKAPI_ATTR void VKAPI_CALL DestroyDescriptorUpdateTemplateKHR(
    VkDevice                                    device,
    VkDescriptorUpdateTemplateKHR               descriptorUpdateTemplate,
    const VkAllocationCallbacks*                pAllocator) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    disp->DestroyDescriptorUpdateTemplateKHR(device, descriptorUpdateTemplate, pAllocator);
}

VKAPI_ATTR void VKAPI_CALL UpdateDescriptorSetWithTemplateKHR(
    VkDevice                                    device,
    VkDescriptorSet                             descriptorSet,
    VkDescriptorUpdateTemplateKHR               descriptorUpdateTemplate,
    const void*                                 pData) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    disp->UpdateDescriptorSetWithTemplateKHR(device, descriptorSet, descriptorUpdateTemplate, pData);
}

VKAPI_ATTR void VKAPI_CALL CmdPushDescriptorSetWithTemplateKHR(
    VkCommandBuffer                             commandBuffer,
    VkDescriptorUpdateTemplateKHR               descriptorUpdateTemplate,
    VkPipelineLayout                            layout,
    uint32_t                                    set,
    const void*                                 pData) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdPushDescriptorSetWithTemplateKHR(commandBuffer, descriptorUpdateTemplate, layout, set, pData);
}


// ---- VK_KHR_shared_presentable_image extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL GetSwapchainStatusKHR(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetSwapchainStatusKHR(device, swapchain);
}


// ---- VK_KHR_external_fence_win32 extension trampoline/terminators

#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL ImportFenceWin32HandleKHR(
    VkDevice                                    device,
    const VkImportFenceWin32HandleInfoKHR*      pImportFenceWin32HandleInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->ImportFenceWin32HandleKHR(device, pImportFenceWin32HandleInfo);
}

#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL GetFenceWin32HandleKHR(
    VkDevice                                    device,
    const VkFenceGetWin32HandleInfoKHR*         pGetWin32HandleInfo,
    HANDLE*                                     pHandle) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetFenceWin32HandleKHR(device, pGetWin32HandleInfo, pHandle);
}

#endif // VK_USE_PLATFORM_WIN32_KHR

// ---- VK_KHR_external_fence_fd extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL ImportFenceFdKHR(
    VkDevice                                    device,
    const VkImportFenceFdInfoKHR*               pImportFenceFdInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->ImportFenceFdKHR(device, pImportFenceFdInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL GetFenceFdKHR(
    VkDevice                                    device,
    const VkFenceGetFdInfoKHR*                  pGetFdInfo,
    int*                                        pFd) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetFenceFdKHR(device, pGetFdInfo, pFd);
}


// ---- VK_KHR_get_memory_requirements2 extension trampoline/terminators

VKAPI_ATTR void VKAPI_CALL GetImageMemoryRequirements2KHR(
    VkDevice                                    device,
    const VkImageMemoryRequirementsInfo2KHR*    pInfo,
    VkMemoryRequirements2KHR*                   pMemoryRequirements) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    disp->GetImageMemoryRequirements2KHR(device, pInfo, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL GetBufferMemoryRequirements2KHR(
    VkDevice                                    device,
    const VkBufferMemoryRequirementsInfo2KHR*   pInfo,
    VkMemoryRequirements2KHR*                   pMemoryRequirements) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    disp->GetBufferMemoryRequirements2KHR(device, pInfo, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL GetImageSparseMemoryRequirements2KHR(
    VkDevice                                    device,
    const VkImageSparseMemoryRequirementsInfo2KHR* pInfo,
    uint32_t*                                   pSparseMemoryRequirementCount,
    VkSparseImageMemoryRequirements2KHR*        pSparseMemoryRequirements) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    disp->GetImageSparseMemoryRequirements2KHR(device, pInfo, pSparseMemoryRequirementCount, pSparseMemoryRequirements);
}


// ---- VK_KHR_sampler_ycbcr_conversion extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL CreateSamplerYcbcrConversionKHR(
    VkDevice                                    device,
    const VkSamplerYcbcrConversionCreateInfoKHR* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSamplerYcbcrConversionKHR*                pYcbcrConversion) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->CreateSamplerYcbcrConversionKHR(device, pCreateInfo, pAllocator, pYcbcrConversion);
}

VKAPI_ATTR void VKAPI_CALL DestroySamplerYcbcrConversionKHR(
    VkDevice                                    device,
    VkSamplerYcbcrConversionKHR                 ycbcrConversion,
    const VkAllocationCallbacks*                pAllocator) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    disp->DestroySamplerYcbcrConversionKHR(device, ycbcrConversion, pAllocator);
}


// ---- VK_KHR_bind_memory2 extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL BindBufferMemory2KHR(
    VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindBufferMemoryInfoKHR*            pBindInfos) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->BindBufferMemory2KHR(device, bindInfoCount, pBindInfos);
}

VKAPI_ATTR VkResult VKAPI_CALL BindImageMemory2KHR(
    VkDevice                                    device,
    uint32_t                                    bindInfoCount,
    const VkBindImageMemoryInfoKHR*             pBindInfos) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->BindImageMemory2KHR(device, bindInfoCount, pBindInfos);
}


// ---- VK_EXT_debug_marker extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL DebugMarkerSetObjectTagEXT(
    VkDevice                                    device,
    const VkDebugMarkerObjectTagInfoEXT*        pTagInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    VkDebugMarkerObjectTagInfoEXT local_tag_info;
    memcpy(&local_tag_info, pTagInfo, sizeof(VkDebugMarkerObjectTagInfoEXT));
    // If this is a physical device, we have to replace it with the proper one for the next call.
    if (pTagInfo->objectType == VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT) {
        struct loader_physical_device_tramp *phys_dev_tramp = (struct loader_physical_device_tramp *)(uintptr_t)pTagInfo->object;
        local_tag_info.object = (uint64_t)(uintptr_t)phys_dev_tramp->phys_dev;
    }
    return disp->DebugMarkerSetObjectTagEXT(device, &local_tag_info);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_DebugMarkerSetObjectTagEXT(
    VkDevice                                    device,
    const VkDebugMarkerObjectTagInfoEXT*        pTagInfo) {
    uint32_t icd_index = 0;
    struct loader_device *dev;
    struct loader_icd_term *icd_term = loader_get_icd_and_device(device, &dev, &icd_index);
    if (NULL != icd_term && NULL != icd_term->dispatch.DebugMarkerSetObjectTagEXT) {
        VkDebugMarkerObjectTagInfoEXT local_tag_info;
        memcpy(&local_tag_info, pTagInfo, sizeof(VkDebugMarkerObjectTagInfoEXT));
        // If this is a physical device, we have to replace it with the proper one for the next call.
        if (pTagInfo->objectType == VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT) {
            struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)(uintptr_t)pTagInfo->object;
            local_tag_info.object = (uint64_t)(uintptr_t)phys_dev_term->phys_dev;
        // If this is a KHR_surface, and the ICD has created its own, we have to replace it with the proper one for the next call.
        } else if (pTagInfo->objectType == VK_DEBUG_REPORT_OBJECT_TYPE_SURFACE_KHR_EXT) {
            if (NULL != icd_term && NULL != icd_term->dispatch.CreateSwapchainKHR) {
                VkIcdSurface *icd_surface = (VkIcdSurface *)(uintptr_t)pTagInfo->object;
                if (NULL != icd_surface->real_icd_surfaces) {
                    local_tag_info.object = (uint64_t)icd_surface->real_icd_surfaces[icd_index];
                }
            }
        }
        return icd_term->dispatch.DebugMarkerSetObjectTagEXT(device, &local_tag_info);
    } else {
        return VK_SUCCESS;
    }
}

VKAPI_ATTR VkResult VKAPI_CALL DebugMarkerSetObjectNameEXT(
    VkDevice                                    device,
    const VkDebugMarkerObjectNameInfoEXT*       pNameInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    VkDebugMarkerObjectNameInfoEXT local_name_info;
    memcpy(&local_name_info, pNameInfo, sizeof(VkDebugMarkerObjectNameInfoEXT));
    // If this is a physical device, we have to replace it with the proper one for the next call.
    if (pNameInfo->objectType == VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT) {
        struct loader_physical_device_tramp *phys_dev_tramp = (struct loader_physical_device_tramp *)(uintptr_t)pNameInfo->object;
        local_name_info.object = (uint64_t)(uintptr_t)phys_dev_tramp->phys_dev;
    }
    return disp->DebugMarkerSetObjectNameEXT(device, &local_name_info);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_DebugMarkerSetObjectNameEXT(
    VkDevice                                    device,
    const VkDebugMarkerObjectNameInfoEXT*       pNameInfo) {
    uint32_t icd_index = 0;
    struct loader_device *dev;
    struct loader_icd_term *icd_term = loader_get_icd_and_device(device, &dev, &icd_index);
    if (NULL != icd_term && NULL != icd_term->dispatch.DebugMarkerSetObjectNameEXT) {
        VkDebugMarkerObjectNameInfoEXT local_name_info;
        memcpy(&local_name_info, pNameInfo, sizeof(VkDebugMarkerObjectNameInfoEXT));
        // If this is a physical device, we have to replace it with the proper one for the next call.
        if (pNameInfo->objectType == VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT) {
            struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)(uintptr_t)pNameInfo->object;
            local_name_info.object = (uint64_t)(uintptr_t)phys_dev_term->phys_dev;
        // If this is a KHR_surface, and the ICD has created its own, we have to replace it with the proper one for the next call.
        } else if (pNameInfo->objectType == VK_DEBUG_REPORT_OBJECT_TYPE_SURFACE_KHR_EXT) {
            if (NULL != icd_term && NULL != icd_term->dispatch.CreateSwapchainKHR) {
                VkIcdSurface *icd_surface = (VkIcdSurface *)(uintptr_t)pNameInfo->object;
                if (NULL != icd_surface->real_icd_surfaces) {
                    local_name_info.object = (uint64_t)icd_surface->real_icd_surfaces[icd_index];
                }
            }
        }
        return icd_term->dispatch.DebugMarkerSetObjectNameEXT(device, &local_name_info);
    } else {
        return VK_SUCCESS;
    }
}

VKAPI_ATTR void VKAPI_CALL CmdDebugMarkerBeginEXT(
    VkCommandBuffer                             commandBuffer,
    const VkDebugMarkerMarkerInfoEXT*           pMarkerInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdDebugMarkerBeginEXT(commandBuffer, pMarkerInfo);
}

VKAPI_ATTR void VKAPI_CALL CmdDebugMarkerEndEXT(
    VkCommandBuffer                             commandBuffer) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdDebugMarkerEndEXT(commandBuffer);
}

VKAPI_ATTR void VKAPI_CALL CmdDebugMarkerInsertEXT(
    VkCommandBuffer                             commandBuffer,
    const VkDebugMarkerMarkerInfoEXT*           pMarkerInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdDebugMarkerInsertEXT(commandBuffer, pMarkerInfo);
}


// ---- VK_AMD_draw_indirect_count extension trampoline/terminators

VKAPI_ATTR void VKAPI_CALL CmdDrawIndirectCountAMD(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdDrawIndirectCountAMD(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}

VKAPI_ATTR void VKAPI_CALL CmdDrawIndexedIndirectCountAMD(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkBuffer                                    countBuffer,
    VkDeviceSize                                countBufferOffset,
    uint32_t                                    maxDrawCount,
    uint32_t                                    stride) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdDrawIndexedIndirectCountAMD(commandBuffer, buffer, offset, countBuffer, countBufferOffset, maxDrawCount, stride);
}


// ---- VK_AMD_shader_info extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL GetShaderInfoAMD(
    VkDevice                                    device,
    VkPipeline                                  pipeline,
    VkShaderStageFlagBits                       shaderStage,
    VkShaderInfoTypeAMD                         infoType,
    size_t*                                     pInfoSize,
    void*                                       pInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetShaderInfoAMD(device, pipeline, shaderStage, infoType, pInfoSize, pInfo);
}


// ---- VK_NV_external_memory_win32 extension trampoline/terminators

#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL GetMemoryWin32HandleNV(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkExternalMemoryHandleTypeFlagsNV           handleType,
    HANDLE*                                     pHandle) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetMemoryWin32HandleNV(device, memory, handleType, pHandle);
}

#endif // VK_USE_PLATFORM_WIN32_KHR

// ---- VK_KHX_device_group extension trampoline/terminators

VKAPI_ATTR void VKAPI_CALL GetDeviceGroupPeerMemoryFeaturesKHX(
    VkDevice                                    device,
    uint32_t                                    heapIndex,
    uint32_t                                    localDeviceIndex,
    uint32_t                                    remoteDeviceIndex,
    VkPeerMemoryFeatureFlagsKHX*                pPeerMemoryFeatures) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    disp->GetDeviceGroupPeerMemoryFeaturesKHX(device, heapIndex, localDeviceIndex, remoteDeviceIndex, pPeerMemoryFeatures);
}

VKAPI_ATTR void VKAPI_CALL CmdSetDeviceMaskKHX(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    deviceMask) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdSetDeviceMaskKHX(commandBuffer, deviceMask);
}

VKAPI_ATTR void VKAPI_CALL CmdDispatchBaseKHX(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    baseGroupX,
    uint32_t                                    baseGroupY,
    uint32_t                                    baseGroupZ,
    uint32_t                                    groupCountX,
    uint32_t                                    groupCountY,
    uint32_t                                    groupCountZ) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdDispatchBaseKHX(commandBuffer, baseGroupX, baseGroupY, baseGroupZ, groupCountX, groupCountY, groupCountZ);
}

VKAPI_ATTR VkResult VKAPI_CALL GetDeviceGroupPresentCapabilitiesKHX(
    VkDevice                                    device,
    VkDeviceGroupPresentCapabilitiesKHX*        pDeviceGroupPresentCapabilities) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetDeviceGroupPresentCapabilitiesKHX(device, pDeviceGroupPresentCapabilities);
}

VKAPI_ATTR VkResult VKAPI_CALL GetDeviceGroupSurfacePresentModesKHX(
    VkDevice                                    device,
    VkSurfaceKHR                                surface,
    VkDeviceGroupPresentModeFlagsKHX*           pModes) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetDeviceGroupSurfacePresentModesKHX(device, surface, pModes);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_GetDeviceGroupSurfacePresentModesKHX(
    VkDevice                                    device,
    VkSurfaceKHR                                surface,
    VkDeviceGroupPresentModeFlagsKHX*           pModes) {
    uint32_t icd_index = 0;
    struct loader_device *dev;
    struct loader_icd_term *icd_term = loader_get_icd_and_device(device, &dev, &icd_index);
    if (NULL != icd_term && NULL != icd_term->dispatch.GetDeviceGroupSurfacePresentModesKHX) {
        VkIcdSurface *icd_surface = (VkIcdSurface *)(uintptr_t)surface;
        if (NULL != icd_surface->real_icd_surfaces && (VkSurfaceKHR)NULL != icd_surface->real_icd_surfaces[icd_index]) {
            return icd_term->dispatch.GetDeviceGroupSurfacePresentModesKHX(device, icd_surface->real_icd_surfaces[icd_index], pModes);
        }
        return icd_term->dispatch.GetDeviceGroupSurfacePresentModesKHX(device, surface, pModes);
    }
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL GetPhysicalDevicePresentRectanglesKHX(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pRectCount,
    VkRect2D*                                   pRects) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    return disp->GetPhysicalDevicePresentRectanglesKHX(unwrapped_phys_dev, surface, pRectCount, pRects);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_GetPhysicalDevicePresentRectanglesKHX(
    VkPhysicalDevice                            physicalDevice,
    VkSurfaceKHR                                surface,
    uint32_t*                                   pRectCount,
    VkRect2D*                                   pRects) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL == icd_term->dispatch.GetPhysicalDevicePresentRectanglesKHX) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "ICD associated with VkPhysicalDevice does not support GetPhysicalDevicePresentRectanglesKHX");
    }
    VkIcdSurface *icd_surface = (VkIcdSurface *)(surface);
    uint8_t icd_index = phys_dev_term->icd_index;
    if (NULL != icd_surface->real_icd_surfaces && NULL != (void *)icd_surface->real_icd_surfaces[icd_index]) {
        return icd_term->dispatch.GetPhysicalDevicePresentRectanglesKHX(phys_dev_term->phys_dev, icd_surface->real_icd_surfaces[icd_index], pRectCount, pRects);
    }
    return icd_term->dispatch.GetPhysicalDevicePresentRectanglesKHX(phys_dev_term->phys_dev, surface, pRectCount, pRects);
}

VKAPI_ATTR VkResult VKAPI_CALL AcquireNextImage2KHX(
    VkDevice                                    device,
    const VkAcquireNextImageInfoKHX*            pAcquireInfo,
    uint32_t*                                   pImageIndex) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->AcquireNextImage2KHX(device, pAcquireInfo, pImageIndex);
}


// ---- VK_NN_vi_surface extension trampoline/terminators

#ifdef VK_USE_PLATFORM_VI_NN
VKAPI_ATTR VkResult VKAPI_CALL CreateViSurfaceNN(
    VkInstance                                  instance,
    const VkViSurfaceCreateInfoNN*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) {
#error("Not implemented. Likely needs to be manually generated!");
    return disp->CreateViSurfaceNN(instance, pCreateInfo, pAllocator, pSurface);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_CreateViSurfaceNN(
    VkInstance                                  instance,
    const VkViSurfaceCreateInfoNN*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) {
#error("Not implemented. Likely needs to be manually generated!");
}

#endif // VK_USE_PLATFORM_VI_NN

// ---- VK_NVX_device_generated_commands extension trampoline/terminators

VKAPI_ATTR void VKAPI_CALL CmdProcessCommandsNVX(
    VkCommandBuffer                             commandBuffer,
    const VkCmdProcessCommandsInfoNVX*          pProcessCommandsInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdProcessCommandsNVX(commandBuffer, pProcessCommandsInfo);
}

VKAPI_ATTR void VKAPI_CALL CmdReserveSpaceForCommandsNVX(
    VkCommandBuffer                             commandBuffer,
    const VkCmdReserveSpaceForCommandsInfoNVX*  pReserveSpaceInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdReserveSpaceForCommandsNVX(commandBuffer, pReserveSpaceInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateIndirectCommandsLayoutNVX(
    VkDevice                                    device,
    const VkIndirectCommandsLayoutCreateInfoNVX* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkIndirectCommandsLayoutNVX*                pIndirectCommandsLayout) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->CreateIndirectCommandsLayoutNVX(device, pCreateInfo, pAllocator, pIndirectCommandsLayout);
}

VKAPI_ATTR void VKAPI_CALL DestroyIndirectCommandsLayoutNVX(
    VkDevice                                    device,
    VkIndirectCommandsLayoutNVX                 indirectCommandsLayout,
    const VkAllocationCallbacks*                pAllocator) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    disp->DestroyIndirectCommandsLayoutNVX(device, indirectCommandsLayout, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateObjectTableNVX(
    VkDevice                                    device,
    const VkObjectTableCreateInfoNVX*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkObjectTableNVX*                           pObjectTable) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->CreateObjectTableNVX(device, pCreateInfo, pAllocator, pObjectTable);
}

VKAPI_ATTR void VKAPI_CALL DestroyObjectTableNVX(
    VkDevice                                    device,
    VkObjectTableNVX                            objectTable,
    const VkAllocationCallbacks*                pAllocator) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    disp->DestroyObjectTableNVX(device, objectTable, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL RegisterObjectsNVX(
    VkDevice                                    device,
    VkObjectTableNVX                            objectTable,
    uint32_t                                    objectCount,
    const VkObjectTableEntryNVX* const*         ppObjectTableEntries,
    const uint32_t*                             pObjectIndices) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->RegisterObjectsNVX(device, objectTable, objectCount, ppObjectTableEntries, pObjectIndices);
}

VKAPI_ATTR VkResult VKAPI_CALL UnregisterObjectsNVX(
    VkDevice                                    device,
    VkObjectTableNVX                            objectTable,
    uint32_t                                    objectCount,
    const VkObjectEntryTypeNVX*                 pObjectEntryTypes,
    const uint32_t*                             pObjectIndices) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->UnregisterObjectsNVX(device, objectTable, objectCount, pObjectEntryTypes, pObjectIndices);
}

VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceGeneratedCommandsPropertiesNVX(
    VkPhysicalDevice                            physicalDevice,
    VkDeviceGeneratedCommandsFeaturesNVX*       pFeatures,
    VkDeviceGeneratedCommandsLimitsNVX*         pLimits) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceGeneratedCommandsPropertiesNVX(unwrapped_phys_dev, pFeatures, pLimits);
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceGeneratedCommandsPropertiesNVX(
    VkPhysicalDevice                            physicalDevice,
    VkDeviceGeneratedCommandsFeaturesNVX*       pFeatures,
    VkDeviceGeneratedCommandsLimitsNVX*         pLimits) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL == icd_term->dispatch.GetPhysicalDeviceGeneratedCommandsPropertiesNVX) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "ICD associated with VkPhysicalDevice does not support GetPhysicalDeviceGeneratedCommandsPropertiesNVX");
    }
    icd_term->dispatch.GetPhysicalDeviceGeneratedCommandsPropertiesNVX(phys_dev_term->phys_dev, pFeatures, pLimits);
}


// ---- VK_NV_clip_space_w_scaling extension trampoline/terminators

VKAPI_ATTR void VKAPI_CALL CmdSetViewportWScalingNV(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstViewport,
    uint32_t                                    viewportCount,
    const VkViewportWScalingNV*                 pViewportWScalings) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdSetViewportWScalingNV(commandBuffer, firstViewport, viewportCount, pViewportWScalings);
}


// ---- VK_EXT_display_control extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL DisplayPowerControlEXT(
    VkDevice                                    device,
    VkDisplayKHR                                display,
    const VkDisplayPowerInfoEXT*                pDisplayPowerInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->DisplayPowerControlEXT(device, display, pDisplayPowerInfo);
}

VKAPI_ATTR VkResult VKAPI_CALL RegisterDeviceEventEXT(
    VkDevice                                    device,
    const VkDeviceEventInfoEXT*                 pDeviceEventInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->RegisterDeviceEventEXT(device, pDeviceEventInfo, pAllocator, pFence);
}

VKAPI_ATTR VkResult VKAPI_CALL RegisterDisplayEventEXT(
    VkDevice                                    device,
    VkDisplayKHR                                display,
    const VkDisplayEventInfoEXT*                pDisplayEventInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkFence*                                    pFence) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->RegisterDisplayEventEXT(device, display, pDisplayEventInfo, pAllocator, pFence);
}

VKAPI_ATTR VkResult VKAPI_CALL GetSwapchainCounterEXT(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkSurfaceCounterFlagBitsEXT                 counter,
    uint64_t*                                   pCounterValue) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetSwapchainCounterEXT(device, swapchain, counter, pCounterValue);
}


// ---- VK_GOOGLE_display_timing extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL GetRefreshCycleDurationGOOGLE(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    VkRefreshCycleDurationGOOGLE*               pDisplayTimingProperties) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetRefreshCycleDurationGOOGLE(device, swapchain, pDisplayTimingProperties);
}

VKAPI_ATTR VkResult VKAPI_CALL GetPastPresentationTimingGOOGLE(
    VkDevice                                    device,
    VkSwapchainKHR                              swapchain,
    uint32_t*                                   pPresentationTimingCount,
    VkPastPresentationTimingGOOGLE*             pPresentationTimings) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetPastPresentationTimingGOOGLE(device, swapchain, pPresentationTimingCount, pPresentationTimings);
}


// ---- VK_EXT_discard_rectangles extension trampoline/terminators

VKAPI_ATTR void VKAPI_CALL CmdSetDiscardRectangleEXT(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstDiscardRectangle,
    uint32_t                                    discardRectangleCount,
    const VkRect2D*                             pDiscardRectangles) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdSetDiscardRectangleEXT(commandBuffer, firstDiscardRectangle, discardRectangleCount, pDiscardRectangles);
}


// ---- VK_EXT_hdr_metadata extension trampoline/terminators

VKAPI_ATTR void VKAPI_CALL SetHdrMetadataEXT(
    VkDevice                                    device,
    uint32_t                                    swapchainCount,
    const VkSwapchainKHR*                       pSwapchains,
    const VkHdrMetadataEXT*                     pMetadata) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    disp->SetHdrMetadataEXT(device, swapchainCount, pSwapchains, pMetadata);
}


// ---- VK_MVK_ios_surface extension trampoline/terminators

#ifdef VK_USE_PLATFORM_IOS_MVK
VKAPI_ATTR VkResult VKAPI_CALL CreateIOSSurfaceMVK(
    VkInstance                                  instance,
    const VkIOSSurfaceCreateInfoMVK*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) {
#error("Not implemented. Likely needs to be manually generated!");
    return disp->CreateIOSSurfaceMVK(instance, pCreateInfo, pAllocator, pSurface);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_CreateIOSSurfaceMVK(
    VkInstance                                  instance,
    const VkIOSSurfaceCreateInfoMVK*            pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) {
#error("Not implemented. Likely needs to be manually generated!");
}

#endif // VK_USE_PLATFORM_IOS_MVK

// ---- VK_MVK_macos_surface extension trampoline/terminators

#ifdef VK_USE_PLATFORM_MACOS_MVK
VKAPI_ATTR VkResult VKAPI_CALL CreateMacOSSurfaceMVK(
    VkInstance                                  instance,
    const VkMacOSSurfaceCreateInfoMVK*          pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) {
#error("Not implemented. Likely needs to be manually generated!");
    return disp->CreateMacOSSurfaceMVK(instance, pCreateInfo, pAllocator, pSurface);
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_CreateMacOSSurfaceMVK(
    VkInstance                                  instance,
    const VkMacOSSurfaceCreateInfoMVK*          pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSurfaceKHR*                               pSurface) {
#error("Not implemented. Likely needs to be manually generated!");
}

#endif // VK_USE_PLATFORM_MACOS_MVK

// ---- VK_EXT_sample_locations extension trampoline/terminators

VKAPI_ATTR void VKAPI_CALL CmdSetSampleLocationsEXT(
    VkCommandBuffer                             commandBuffer,
    const VkSampleLocationsInfoEXT*             pSampleLocationsInfo) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(commandBuffer);
    disp->CmdSetSampleLocationsEXT(commandBuffer, pSampleLocationsInfo);
}

VKAPI_ATTR void VKAPI_CALL GetPhysicalDeviceMultisamplePropertiesEXT(
    VkPhysicalDevice                            physicalDevice,
    VkSampleCountFlagBits                       samples,
    VkMultisamplePropertiesEXT*                 pMultisampleProperties) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev = loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_layer_dispatch(physicalDevice);
    disp->GetPhysicalDeviceMultisamplePropertiesEXT(unwrapped_phys_dev, samples, pMultisampleProperties);
}

VKAPI_ATTR void VKAPI_CALL terminator_GetPhysicalDeviceMultisamplePropertiesEXT(
    VkPhysicalDevice                            physicalDevice,
    VkSampleCountFlagBits                       samples,
    VkMultisamplePropertiesEXT*                 pMultisampleProperties) {
    struct loader_physical_device_term *phys_dev_term = (struct loader_physical_device_term *)physicalDevice;
    struct loader_icd_term *icd_term = phys_dev_term->this_icd_term;
    if (NULL == icd_term->dispatch.GetPhysicalDeviceMultisamplePropertiesEXT) {
        loader_log(icd_term->this_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "ICD associated with VkPhysicalDevice does not support GetPhysicalDeviceMultisamplePropertiesEXT");
    }
    icd_term->dispatch.GetPhysicalDeviceMultisamplePropertiesEXT(phys_dev_term->phys_dev, samples, pMultisampleProperties);
}


// ---- VK_EXT_validation_cache extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL CreateValidationCacheEXT(
    VkDevice                                    device,
    const VkValidationCacheCreateInfoEXT*       pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkValidationCacheEXT*                       pValidationCache) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->CreateValidationCacheEXT(device, pCreateInfo, pAllocator, pValidationCache);
}

VKAPI_ATTR void VKAPI_CALL DestroyValidationCacheEXT(
    VkDevice                                    device,
    VkValidationCacheEXT                        validationCache,
    const VkAllocationCallbacks*                pAllocator) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    disp->DestroyValidationCacheEXT(device, validationCache, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL MergeValidationCachesEXT(
    VkDevice                                    device,
    VkValidationCacheEXT                        dstCache,
    uint32_t                                    srcCacheCount,
    const VkValidationCacheEXT*                 pSrcCaches) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->MergeValidationCachesEXT(device, dstCache, srcCacheCount, pSrcCaches);
}

VKAPI_ATTR VkResult VKAPI_CALL GetValidationCacheDataEXT(
    VkDevice                                    device,
    VkValidationCacheEXT                        validationCache,
    size_t*                                     pDataSize,
    void*                                       pData) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetValidationCacheDataEXT(device, validationCache, pDataSize, pData);
}


// ---- VK_EXT_external_memory_host extension trampoline/terminators

VKAPI_ATTR VkResult VKAPI_CALL GetMemoryHostPointerPropertiesEXT(
    VkDevice                                    device,
    VkExternalMemoryHandleTypeFlagBitsKHR       handleType,
    const void*                                 pHostPointer,
    VkMemoryHostPointerPropertiesEXT*           pMemoryHostPointerProperties) {
    const VkLayerDispatchTable *disp = loader_get_dispatch(device);
    return disp->GetMemoryHostPointerPropertiesEXT(device, handleType, pHostPointer, pMemoryHostPointerProperties);
}

// GPA helpers for extensions
bool extension_instance_gpa(struct loader_instance *ptr_instance, const char *name, void **addr) {
    *addr = NULL;


    // ---- VK_KHR_get_physical_device_properties2 extension commands
    if (!strcmp("vkGetPhysicalDeviceFeatures2KHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions.khr_get_physical_device_properties2 == 1)
                     ? (void *)GetPhysicalDeviceFeatures2KHR
                     : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceProperties2KHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions.khr_get_physical_device_properties2 == 1)
                     ? (void *)GetPhysicalDeviceProperties2KHR
                     : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceFormatProperties2KHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions.khr_get_physical_device_properties2 == 1)
                     ? (void *)GetPhysicalDeviceFormatProperties2KHR
                     : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceImageFormatProperties2KHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions.khr_get_physical_device_properties2 == 1)
                     ? (void *)GetPhysicalDeviceImageFormatProperties2KHR
                     : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceQueueFamilyProperties2KHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions.khr_get_physical_device_properties2 == 1)
                     ? (void *)GetPhysicalDeviceQueueFamilyProperties2KHR
                     : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceMemoryProperties2KHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions.khr_get_physical_device_properties2 == 1)
                     ? (void *)GetPhysicalDeviceMemoryProperties2KHR
                     : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceSparseImageFormatProperties2KHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions.khr_get_physical_device_properties2 == 1)
                     ? (void *)GetPhysicalDeviceSparseImageFormatProperties2KHR
                     : NULL;
        return true;
    }

    // ---- VK_KHR_maintenance1 extension commands
    if (!strcmp("vkTrimCommandPoolKHR", name)) {
        *addr = (void *)TrimCommandPoolKHR;
        return true;
    }

    // ---- VK_KHR_external_memory_capabilities extension commands
    if (!strcmp("vkGetPhysicalDeviceExternalBufferPropertiesKHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions.khr_external_memory_capabilities == 1)
                     ? (void *)GetPhysicalDeviceExternalBufferPropertiesKHR
                     : NULL;
        return true;
    }

    // ---- VK_KHR_external_memory_win32 extension commands
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp("vkGetMemoryWin32HandleKHR", name)) {
        *addr = (void *)GetMemoryWin32HandleKHR;
        return true;
    }
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp("vkGetMemoryWin32HandlePropertiesKHR", name)) {
        *addr = (void *)GetMemoryWin32HandlePropertiesKHR;
        return true;
    }
#endif // VK_USE_PLATFORM_WIN32_KHR

    // ---- VK_KHR_external_memory_fd extension commands
    if (!strcmp("vkGetMemoryFdKHR", name)) {
        *addr = (void *)GetMemoryFdKHR;
        return true;
    }
    if (!strcmp("vkGetMemoryFdPropertiesKHR", name)) {
        *addr = (void *)GetMemoryFdPropertiesKHR;
        return true;
    }

    // ---- VK_KHR_external_semaphore_capabilities extension commands
    if (!strcmp("vkGetPhysicalDeviceExternalSemaphorePropertiesKHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions.khr_external_semaphore_capabilities == 1)
                     ? (void *)GetPhysicalDeviceExternalSemaphorePropertiesKHR
                     : NULL;
        return true;
    }

    // ---- VK_KHR_external_semaphore_win32 extension commands
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp("vkImportSemaphoreWin32HandleKHR", name)) {
        *addr = (void *)ImportSemaphoreWin32HandleKHR;
        return true;
    }
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp("vkGetSemaphoreWin32HandleKHR", name)) {
        *addr = (void *)GetSemaphoreWin32HandleKHR;
        return true;
    }
#endif // VK_USE_PLATFORM_WIN32_KHR

    // ---- VK_KHR_external_semaphore_fd extension commands
    if (!strcmp("vkImportSemaphoreFdKHR", name)) {
        *addr = (void *)ImportSemaphoreFdKHR;
        return true;
    }
    if (!strcmp("vkGetSemaphoreFdKHR", name)) {
        *addr = (void *)GetSemaphoreFdKHR;
        return true;
    }

    // ---- VK_KHR_push_descriptor extension commands
    if (!strcmp("vkCmdPushDescriptorSetKHR", name)) {
        *addr = (void *)CmdPushDescriptorSetKHR;
        return true;
    }

    // ---- VK_KHR_descriptor_update_template extension commands
    if (!strcmp("vkCreateDescriptorUpdateTemplateKHR", name)) {
        *addr = (void *)CreateDescriptorUpdateTemplateKHR;
        return true;
    }
    if (!strcmp("vkDestroyDescriptorUpdateTemplateKHR", name)) {
        *addr = (void *)DestroyDescriptorUpdateTemplateKHR;
        return true;
    }
    if (!strcmp("vkUpdateDescriptorSetWithTemplateKHR", name)) {
        *addr = (void *)UpdateDescriptorSetWithTemplateKHR;
        return true;
    }
    if (!strcmp("vkCmdPushDescriptorSetWithTemplateKHR", name)) {
        *addr = (void *)CmdPushDescriptorSetWithTemplateKHR;
        return true;
    }

    // ---- VK_KHR_shared_presentable_image extension commands
    if (!strcmp("vkGetSwapchainStatusKHR", name)) {
        *addr = (void *)GetSwapchainStatusKHR;
        return true;
    }

    // ---- VK_KHR_external_fence_capabilities extension commands
    if (!strcmp("vkGetPhysicalDeviceExternalFencePropertiesKHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions.khr_external_fence_capabilities == 1)
                     ? (void *)GetPhysicalDeviceExternalFencePropertiesKHR
                     : NULL;
        return true;
    }

    // ---- VK_KHR_external_fence_win32 extension commands
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp("vkImportFenceWin32HandleKHR", name)) {
        *addr = (void *)ImportFenceWin32HandleKHR;
        return true;
    }
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp("vkGetFenceWin32HandleKHR", name)) {
        *addr = (void *)GetFenceWin32HandleKHR;
        return true;
    }
#endif // VK_USE_PLATFORM_WIN32_KHR

    // ---- VK_KHR_external_fence_fd extension commands
    if (!strcmp("vkImportFenceFdKHR", name)) {
        *addr = (void *)ImportFenceFdKHR;
        return true;
    }
    if (!strcmp("vkGetFenceFdKHR", name)) {
        *addr = (void *)GetFenceFdKHR;
        return true;
    }

    // ---- VK_KHR_get_surface_capabilities2 extension commands
    if (!strcmp("vkGetPhysicalDeviceSurfaceCapabilities2KHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions.khr_get_surface_capabilities2 == 1)
                     ? (void *)GetPhysicalDeviceSurfaceCapabilities2KHR
                     : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceSurfaceFormats2KHR", name)) {
        *addr = (ptr_instance->enabled_known_extensions.khr_get_surface_capabilities2 == 1)
                     ? (void *)GetPhysicalDeviceSurfaceFormats2KHR
                     : NULL;
        return true;
    }

    // ---- VK_KHR_get_memory_requirements2 extension commands
    if (!strcmp("vkGetImageMemoryRequirements2KHR", name)) {
        *addr = (void *)GetImageMemoryRequirements2KHR;
        return true;
    }
    if (!strcmp("vkGetBufferMemoryRequirements2KHR", name)) {
        *addr = (void *)GetBufferMemoryRequirements2KHR;
        return true;
    }
    if (!strcmp("vkGetImageSparseMemoryRequirements2KHR", name)) {
        *addr = (void *)GetImageSparseMemoryRequirements2KHR;
        return true;
    }

    // ---- VK_KHR_sampler_ycbcr_conversion extension commands
    if (!strcmp("vkCreateSamplerYcbcrConversionKHR", name)) {
        *addr = (void *)CreateSamplerYcbcrConversionKHR;
        return true;
    }
    if (!strcmp("vkDestroySamplerYcbcrConversionKHR", name)) {
        *addr = (void *)DestroySamplerYcbcrConversionKHR;
        return true;
    }

    // ---- VK_KHR_bind_memory2 extension commands
    if (!strcmp("vkBindBufferMemory2KHR", name)) {
        *addr = (void *)BindBufferMemory2KHR;
        return true;
    }
    if (!strcmp("vkBindImageMemory2KHR", name)) {
        *addr = (void *)BindImageMemory2KHR;
        return true;
    }

    // ---- VK_EXT_debug_marker extension commands
    if (!strcmp("vkDebugMarkerSetObjectTagEXT", name)) {
        *addr = (void *)DebugMarkerSetObjectTagEXT;
        return true;
    }
    if (!strcmp("vkDebugMarkerSetObjectNameEXT", name)) {
        *addr = (void *)DebugMarkerSetObjectNameEXT;
        return true;
    }
    if (!strcmp("vkCmdDebugMarkerBeginEXT", name)) {
        *addr = (void *)CmdDebugMarkerBeginEXT;
        return true;
    }
    if (!strcmp("vkCmdDebugMarkerEndEXT", name)) {
        *addr = (void *)CmdDebugMarkerEndEXT;
        return true;
    }
    if (!strcmp("vkCmdDebugMarkerInsertEXT", name)) {
        *addr = (void *)CmdDebugMarkerInsertEXT;
        return true;
    }

    // ---- VK_AMD_draw_indirect_count extension commands
    if (!strcmp("vkCmdDrawIndirectCountAMD", name)) {
        *addr = (void *)CmdDrawIndirectCountAMD;
        return true;
    }
    if (!strcmp("vkCmdDrawIndexedIndirectCountAMD", name)) {
        *addr = (void *)CmdDrawIndexedIndirectCountAMD;
        return true;
    }

    // ---- VK_AMD_shader_info extension commands
    if (!strcmp("vkGetShaderInfoAMD", name)) {
        *addr = (void *)GetShaderInfoAMD;
        return true;
    }

    // ---- VK_NV_external_memory_capabilities extension commands
    if (!strcmp("vkGetPhysicalDeviceExternalImageFormatPropertiesNV", name)) {
        *addr = (ptr_instance->enabled_known_extensions.nv_external_memory_capabilities == 1)
                     ? (void *)GetPhysicalDeviceExternalImageFormatPropertiesNV
                     : NULL;
        return true;
    }

    // ---- VK_NV_external_memory_win32 extension commands
#ifdef VK_USE_PLATFORM_WIN32_KHR
    if (!strcmp("vkGetMemoryWin32HandleNV", name)) {
        *addr = (void *)GetMemoryWin32HandleNV;
        return true;
    }
#endif // VK_USE_PLATFORM_WIN32_KHR

    // ---- VK_KHX_device_group extension commands
    if (!strcmp("vkGetDeviceGroupPeerMemoryFeaturesKHX", name)) {
        *addr = (void *)GetDeviceGroupPeerMemoryFeaturesKHX;
        return true;
    }
    if (!strcmp("vkCmdSetDeviceMaskKHX", name)) {
        *addr = (void *)CmdSetDeviceMaskKHX;
        return true;
    }
    if (!strcmp("vkCmdDispatchBaseKHX", name)) {
        *addr = (void *)CmdDispatchBaseKHX;
        return true;
    }
    if (!strcmp("vkGetDeviceGroupPresentCapabilitiesKHX", name)) {
        *addr = (void *)GetDeviceGroupPresentCapabilitiesKHX;
        return true;
    }
    if (!strcmp("vkGetDeviceGroupSurfacePresentModesKHX", name)) {
        *addr = (void *)GetDeviceGroupSurfacePresentModesKHX;
        return true;
    }
    if (!strcmp("vkGetPhysicalDevicePresentRectanglesKHX", name)) {
        *addr = (void *)GetPhysicalDevicePresentRectanglesKHX;
        return true;
    }
    if (!strcmp("vkAcquireNextImage2KHX", name)) {
        *addr = (void *)AcquireNextImage2KHX;
        return true;
    }

    // ---- VK_NN_vi_surface extension commands
#ifdef VK_USE_PLATFORM_VI_NN
    if (!strcmp("vkCreateViSurfaceNN", name)) {
        *addr = (ptr_instance->enabled_known_extensions.nn_vi_surface == 1)
                     ? (void *)CreateViSurfaceNN
                     : NULL;
        return true;
    }
#endif // VK_USE_PLATFORM_VI_NN

    // ---- VK_KHX_device_group_creation extension commands
    if (!strcmp("vkEnumeratePhysicalDeviceGroupsKHX", name)) {
        *addr = (ptr_instance->enabled_known_extensions.khx_device_group_creation == 1)
                     ? (void *)EnumeratePhysicalDeviceGroupsKHX
                     : NULL;
        return true;
    }

    // ---- VK_NVX_device_generated_commands extension commands
    if (!strcmp("vkCmdProcessCommandsNVX", name)) {
        *addr = (void *)CmdProcessCommandsNVX;
        return true;
    }
    if (!strcmp("vkCmdReserveSpaceForCommandsNVX", name)) {
        *addr = (void *)CmdReserveSpaceForCommandsNVX;
        return true;
    }
    if (!strcmp("vkCreateIndirectCommandsLayoutNVX", name)) {
        *addr = (void *)CreateIndirectCommandsLayoutNVX;
        return true;
    }
    if (!strcmp("vkDestroyIndirectCommandsLayoutNVX", name)) {
        *addr = (void *)DestroyIndirectCommandsLayoutNVX;
        return true;
    }
    if (!strcmp("vkCreateObjectTableNVX", name)) {
        *addr = (void *)CreateObjectTableNVX;
        return true;
    }
    if (!strcmp("vkDestroyObjectTableNVX", name)) {
        *addr = (void *)DestroyObjectTableNVX;
        return true;
    }
    if (!strcmp("vkRegisterObjectsNVX", name)) {
        *addr = (void *)RegisterObjectsNVX;
        return true;
    }
    if (!strcmp("vkUnregisterObjectsNVX", name)) {
        *addr = (void *)UnregisterObjectsNVX;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceGeneratedCommandsPropertiesNVX", name)) {
        *addr = (void *)GetPhysicalDeviceGeneratedCommandsPropertiesNVX;
        return true;
    }

    // ---- VK_NV_clip_space_w_scaling extension commands
    if (!strcmp("vkCmdSetViewportWScalingNV", name)) {
        *addr = (void *)CmdSetViewportWScalingNV;
        return true;
    }

    // ---- VK_EXT_direct_mode_display extension commands
    if (!strcmp("vkReleaseDisplayEXT", name)) {
        *addr = (ptr_instance->enabled_known_extensions.ext_direct_mode_display == 1)
                     ? (void *)ReleaseDisplayEXT
                     : NULL;
        return true;
    }

    // ---- VK_EXT_acquire_xlib_display extension commands
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    if (!strcmp("vkAcquireXlibDisplayEXT", name)) {
        *addr = (ptr_instance->enabled_known_extensions.ext_acquire_xlib_display == 1)
                     ? (void *)AcquireXlibDisplayEXT
                     : NULL;
        return true;
    }
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    if (!strcmp("vkGetRandROutputDisplayEXT", name)) {
        *addr = (ptr_instance->enabled_known_extensions.ext_acquire_xlib_display == 1)
                     ? (void *)GetRandROutputDisplayEXT
                     : NULL;
        return true;
    }
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT

    // ---- VK_EXT_display_surface_counter extension commands
    if (!strcmp("vkGetPhysicalDeviceSurfaceCapabilities2EXT", name)) {
        *addr = (ptr_instance->enabled_known_extensions.ext_display_surface_counter == 1)
                     ? (void *)GetPhysicalDeviceSurfaceCapabilities2EXT
                     : NULL;
        return true;
    }

    // ---- VK_EXT_display_control extension commands
    if (!strcmp("vkDisplayPowerControlEXT", name)) {
        *addr = (void *)DisplayPowerControlEXT;
        return true;
    }
    if (!strcmp("vkRegisterDeviceEventEXT", name)) {
        *addr = (void *)RegisterDeviceEventEXT;
        return true;
    }
    if (!strcmp("vkRegisterDisplayEventEXT", name)) {
        *addr = (void *)RegisterDisplayEventEXT;
        return true;
    }
    if (!strcmp("vkGetSwapchainCounterEXT", name)) {
        *addr = (void *)GetSwapchainCounterEXT;
        return true;
    }

    // ---- VK_GOOGLE_display_timing extension commands
    if (!strcmp("vkGetRefreshCycleDurationGOOGLE", name)) {
        *addr = (void *)GetRefreshCycleDurationGOOGLE;
        return true;
    }
    if (!strcmp("vkGetPastPresentationTimingGOOGLE", name)) {
        *addr = (void *)GetPastPresentationTimingGOOGLE;
        return true;
    }

    // ---- VK_EXT_discard_rectangles extension commands
    if (!strcmp("vkCmdSetDiscardRectangleEXT", name)) {
        *addr = (void *)CmdSetDiscardRectangleEXT;
        return true;
    }

    // ---- VK_EXT_hdr_metadata extension commands
    if (!strcmp("vkSetHdrMetadataEXT", name)) {
        *addr = (void *)SetHdrMetadataEXT;
        return true;
    }

    // ---- VK_MVK_ios_surface extension commands
#ifdef VK_USE_PLATFORM_IOS_MVK
    if (!strcmp("vkCreateIOSSurfaceMVK", name)) {
        *addr = (ptr_instance->enabled_known_extensions.mvk_ios_surface == 1)
                     ? (void *)CreateIOSSurfaceMVK
                     : NULL;
        return true;
    }
#endif // VK_USE_PLATFORM_IOS_MVK

    // ---- VK_MVK_macos_surface extension commands
#ifdef VK_USE_PLATFORM_MACOS_MVK
    if (!strcmp("vkCreateMacOSSurfaceMVK", name)) {
        *addr = (ptr_instance->enabled_known_extensions.mvk_macos_surface == 1)
                     ? (void *)CreateMacOSSurfaceMVK
                     : NULL;
        return true;
    }
#endif // VK_USE_PLATFORM_MACOS_MVK

    // ---- VK_EXT_sample_locations extension commands
    if (!strcmp("vkCmdSetSampleLocationsEXT", name)) {
        *addr = (void *)CmdSetSampleLocationsEXT;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceMultisamplePropertiesEXT", name)) {
        *addr = (void *)GetPhysicalDeviceMultisamplePropertiesEXT;
        return true;
    }

    // ---- VK_EXT_validation_cache extension commands
    if (!strcmp("vkCreateValidationCacheEXT", name)) {
        *addr = (void *)CreateValidationCacheEXT;
        return true;
    }
    if (!strcmp("vkDestroyValidationCacheEXT", name)) {
        *addr = (void *)DestroyValidationCacheEXT;
        return true;
    }
    if (!strcmp("vkMergeValidationCachesEXT", name)) {
        *addr = (void *)MergeValidationCachesEXT;
        return true;
    }
    if (!strcmp("vkGetValidationCacheDataEXT", name)) {
        *addr = (void *)GetValidationCacheDataEXT;
        return true;
    }

    // ---- VK_EXT_external_memory_host extension commands
    if (!strcmp("vkGetMemoryHostPointerPropertiesEXT", name)) {
        *addr = (void *)GetMemoryHostPointerPropertiesEXT;
        return true;
    }
    return false;
}

// A function that can be used to query enabled extensions during a vkCreateInstance call
void extensions_create_instance(struct loader_instance *ptr_instance, const VkInstanceCreateInfo *pCreateInfo) {
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {

    // ---- VK_KHR_get_physical_device_properties2 extension commands
        if (0 == strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions.khr_get_physical_device_properties2 = 1;

    // ---- VK_KHR_external_memory_capabilities extension commands
        } else if (0 == strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions.khr_external_memory_capabilities = 1;

    // ---- VK_KHR_external_semaphore_capabilities extension commands
        } else if (0 == strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions.khr_external_semaphore_capabilities = 1;

    // ---- VK_KHR_external_fence_capabilities extension commands
        } else if (0 == strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions.khr_external_fence_capabilities = 1;

    // ---- VK_KHR_get_surface_capabilities2 extension commands
        } else if (0 == strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions.khr_get_surface_capabilities2 = 1;

    // ---- VK_NV_external_memory_capabilities extension commands
        } else if (0 == strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_NV_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions.nv_external_memory_capabilities = 1;

    // ---- VK_NN_vi_surface extension commands
#ifdef VK_USE_PLATFORM_VI_NN
        } else if (0 == strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_NN_VI_SURFACE_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions.nn_vi_surface = 1;
#endif // VK_USE_PLATFORM_VI_NN

    // ---- VK_KHX_device_group_creation extension commands
        } else if (0 == strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_KHX_DEVICE_GROUP_CREATION_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions.khx_device_group_creation = 1;

    // ---- VK_EXT_direct_mode_display extension commands
        } else if (0 == strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions.ext_direct_mode_display = 1;

    // ---- VK_EXT_acquire_xlib_display extension commands
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
        } else if (0 == strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_EXT_ACQUIRE_XLIB_DISPLAY_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions.ext_acquire_xlib_display = 1;
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT

    // ---- VK_EXT_display_surface_counter extension commands
        } else if (0 == strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions.ext_display_surface_counter = 1;

    // ---- VK_MVK_ios_surface extension commands
#ifdef VK_USE_PLATFORM_IOS_MVK
        } else if (0 == strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_MVK_IOS_SURFACE_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions.mvk_ios_surface = 1;
#endif // VK_USE_PLATFORM_IOS_MVK

    // ---- VK_MVK_macos_surface extension commands
#ifdef VK_USE_PLATFORM_MACOS_MVK
        } else if (0 == strcmp(pCreateInfo->ppEnabledExtensionNames[i], VK_MVK_MACOS_SURFACE_EXTENSION_NAME)) {
            ptr_instance->enabled_known_extensions.mvk_macos_surface = 1;
#endif // VK_USE_PLATFORM_MACOS_MVK
        }
    }
}

// Some device commands still need a terminator because the loader needs to unwrap something about them.
// In many cases, the item needing unwrapping is a VkPhysicalDevice or VkSurfaceKHR object.  But there may be other items
// in the future.
PFN_vkVoidFunction get_extension_device_proc_terminator(const char *pName) {
    PFN_vkVoidFunction addr = NULL;

    // ---- VK_KHR_swapchain extension commands
    if(!strcmp(pName, "vkCreateSwapchainKHR")) {
        addr = (PFN_vkVoidFunction)terminator_CreateSwapchainKHR;

    // ---- VK_KHR_display_swapchain extension commands
    } else if(!strcmp(pName, "vkCreateSharedSwapchainsKHR")) {
        addr = (PFN_vkVoidFunction)terminator_CreateSharedSwapchainsKHR;

    // ---- VK_EXT_debug_marker extension commands
    } else if(!strcmp(pName, "vkDebugMarkerSetObjectTagEXT")) {
        addr = (PFN_vkVoidFunction)terminator_DebugMarkerSetObjectTagEXT;
    } else if(!strcmp(pName, "vkDebugMarkerSetObjectNameEXT")) {
        addr = (PFN_vkVoidFunction)terminator_DebugMarkerSetObjectNameEXT;

    // ---- VK_KHX_device_group extension commands
    } else if(!strcmp(pName, "vkGetDeviceGroupSurfacePresentModesKHX")) {
        addr = (PFN_vkVoidFunction)terminator_GetDeviceGroupSurfacePresentModesKHX;
    }
    return addr;
}

// This table contains the loader's instance dispatch table, which contains
// default functions if no instance layers are activated.  This contains
// pointers to "terminator functions".
const VkLayerInstanceDispatchTable instance_disp = {

    // ---- Core 1_0 commands
    .DestroyInstance = terminator_DestroyInstance,
    .EnumeratePhysicalDevices = terminator_EnumeratePhysicalDevices,
    .GetPhysicalDeviceFeatures = terminator_GetPhysicalDeviceFeatures,
    .GetPhysicalDeviceFormatProperties = terminator_GetPhysicalDeviceFormatProperties,
    .GetPhysicalDeviceImageFormatProperties = terminator_GetPhysicalDeviceImageFormatProperties,
    .GetPhysicalDeviceProperties = terminator_GetPhysicalDeviceProperties,
    .GetPhysicalDeviceQueueFamilyProperties = terminator_GetPhysicalDeviceQueueFamilyProperties,
    .GetPhysicalDeviceMemoryProperties = terminator_GetPhysicalDeviceMemoryProperties,
    .GetInstanceProcAddr = vkGetInstanceProcAddr,
    .EnumerateDeviceExtensionProperties = terminator_EnumerateDeviceExtensionProperties,
    .EnumerateDeviceLayerProperties = terminator_EnumerateDeviceLayerProperties,
    .GetPhysicalDeviceSparseImageFormatProperties = terminator_GetPhysicalDeviceSparseImageFormatProperties,

    // ---- VK_KHR_surface extension commands
    .DestroySurfaceKHR = terminator_DestroySurfaceKHR,
    .GetPhysicalDeviceSurfaceSupportKHR = terminator_GetPhysicalDeviceSurfaceSupportKHR,
    .GetPhysicalDeviceSurfaceCapabilitiesKHR = terminator_GetPhysicalDeviceSurfaceCapabilitiesKHR,
    .GetPhysicalDeviceSurfaceFormatsKHR = terminator_GetPhysicalDeviceSurfaceFormatsKHR,
    .GetPhysicalDeviceSurfacePresentModesKHR = terminator_GetPhysicalDeviceSurfacePresentModesKHR,

    // ---- VK_KHR_display extension commands
    .GetPhysicalDeviceDisplayPropertiesKHR = terminator_GetPhysicalDeviceDisplayPropertiesKHR,
    .GetPhysicalDeviceDisplayPlanePropertiesKHR = terminator_GetPhysicalDeviceDisplayPlanePropertiesKHR,
    .GetDisplayPlaneSupportedDisplaysKHR = terminator_GetDisplayPlaneSupportedDisplaysKHR,
    .GetDisplayModePropertiesKHR = terminator_GetDisplayModePropertiesKHR,
    .CreateDisplayModeKHR = terminator_CreateDisplayModeKHR,
    .GetDisplayPlaneCapabilitiesKHR = terminator_GetDisplayPlaneCapabilitiesKHR,
    .CreateDisplayPlaneSurfaceKHR = terminator_CreateDisplayPlaneSurfaceKHR,

    // ---- VK_KHR_xlib_surface extension commands
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .CreateXlibSurfaceKHR = terminator_CreateXlibSurfaceKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    .GetPhysicalDeviceXlibPresentationSupportKHR = terminator_GetPhysicalDeviceXlibPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XLIB_KHR

    // ---- VK_KHR_xcb_surface extension commands
#ifdef VK_USE_PLATFORM_XCB_KHR
    .CreateXcbSurfaceKHR = terminator_CreateXcbSurfaceKHR,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    .GetPhysicalDeviceXcbPresentationSupportKHR = terminator_GetPhysicalDeviceXcbPresentationSupportKHR,
#endif // VK_USE_PLATFORM_XCB_KHR

    // ---- VK_KHR_wayland_surface extension commands
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .CreateWaylandSurfaceKHR = terminator_CreateWaylandSurfaceKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    .GetPhysicalDeviceWaylandPresentationSupportKHR = terminator_GetPhysicalDeviceWaylandPresentationSupportKHR,
#endif // VK_USE_PLATFORM_WAYLAND_KHR

    // ---- VK_KHR_mir_surface extension commands
#ifdef VK_USE_PLATFORM_MIR_KHR
    .CreateMirSurfaceKHR = terminator_CreateMirSurfaceKHR,
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
    .GetPhysicalDeviceMirPresentationSupportKHR = terminator_GetPhysicalDeviceMirPresentationSupportKHR,
#endif // VK_USE_PLATFORM_MIR_KHR

    // ---- VK_KHR_android_surface extension commands
#ifdef VK_USE_PLATFORM_ANDROID_KHR
    .CreateAndroidSurfaceKHR = terminator_CreateAndroidSurfaceKHR,
#endif // VK_USE_PLATFORM_ANDROID_KHR

    // ---- VK_KHR_win32_surface extension commands
#ifdef VK_USE_PLATFORM_WIN32_KHR
    .CreateWin32SurfaceKHR = terminator_CreateWin32SurfaceKHR,
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
    .GetPhysicalDeviceWin32PresentationSupportKHR = terminator_GetPhysicalDeviceWin32PresentationSupportKHR,
#endif // VK_USE_PLATFORM_WIN32_KHR

    // ---- VK_KHR_get_physical_device_properties2 extension commands
    .GetPhysicalDeviceFeatures2KHR = terminator_GetPhysicalDeviceFeatures2KHR,
    .GetPhysicalDeviceProperties2KHR = terminator_GetPhysicalDeviceProperties2KHR,
    .GetPhysicalDeviceFormatProperties2KHR = terminator_GetPhysicalDeviceFormatProperties2KHR,
    .GetPhysicalDeviceImageFormatProperties2KHR = terminator_GetPhysicalDeviceImageFormatProperties2KHR,
    .GetPhysicalDeviceQueueFamilyProperties2KHR = terminator_GetPhysicalDeviceQueueFamilyProperties2KHR,
    .GetPhysicalDeviceMemoryProperties2KHR = terminator_GetPhysicalDeviceMemoryProperties2KHR,
    .GetPhysicalDeviceSparseImageFormatProperties2KHR = terminator_GetPhysicalDeviceSparseImageFormatProperties2KHR,

    // ---- VK_KHR_external_memory_capabilities extension commands
    .GetPhysicalDeviceExternalBufferPropertiesKHR = terminator_GetPhysicalDeviceExternalBufferPropertiesKHR,

    // ---- VK_KHR_external_semaphore_capabilities extension commands
    .GetPhysicalDeviceExternalSemaphorePropertiesKHR = terminator_GetPhysicalDeviceExternalSemaphorePropertiesKHR,

    // ---- VK_KHR_external_fence_capabilities extension commands
    .GetPhysicalDeviceExternalFencePropertiesKHR = terminator_GetPhysicalDeviceExternalFencePropertiesKHR,

    // ---- VK_KHR_get_surface_capabilities2 extension commands
    .GetPhysicalDeviceSurfaceCapabilities2KHR = terminator_GetPhysicalDeviceSurfaceCapabilities2KHR,
    .GetPhysicalDeviceSurfaceFormats2KHR = terminator_GetPhysicalDeviceSurfaceFormats2KHR,

    // ---- VK_EXT_debug_report extension commands
    .CreateDebugReportCallbackEXT = terminator_CreateDebugReportCallbackEXT,
    .DestroyDebugReportCallbackEXT = terminator_DestroyDebugReportCallbackEXT,
    .DebugReportMessageEXT = terminator_DebugReportMessageEXT,

    // ---- VK_NV_external_memory_capabilities extension commands
    .GetPhysicalDeviceExternalImageFormatPropertiesNV = terminator_GetPhysicalDeviceExternalImageFormatPropertiesNV,

    // ---- VK_KHX_device_group extension commands
    .GetPhysicalDevicePresentRectanglesKHX = terminator_GetPhysicalDevicePresentRectanglesKHX,

    // ---- VK_NN_vi_surface extension commands
#ifdef VK_USE_PLATFORM_VI_NN
    .CreateViSurfaceNN = terminator_CreateViSurfaceNN,
#endif // VK_USE_PLATFORM_VI_NN

    // ---- VK_KHX_device_group_creation extension commands
    .EnumeratePhysicalDeviceGroupsKHX = terminator_EnumeratePhysicalDeviceGroupsKHX,

    // ---- VK_NVX_device_generated_commands extension commands
    .GetPhysicalDeviceGeneratedCommandsPropertiesNVX = terminator_GetPhysicalDeviceGeneratedCommandsPropertiesNVX,

    // ---- VK_EXT_direct_mode_display extension commands
    .ReleaseDisplayEXT = terminator_ReleaseDisplayEXT,

    // ---- VK_EXT_acquire_xlib_display extension commands
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    .AcquireXlibDisplayEXT = terminator_AcquireXlibDisplayEXT,
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
    .GetRandROutputDisplayEXT = terminator_GetRandROutputDisplayEXT,
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT

    // ---- VK_EXT_display_surface_counter extension commands
    .GetPhysicalDeviceSurfaceCapabilities2EXT = terminator_GetPhysicalDeviceSurfaceCapabilities2EXT,

    // ---- VK_MVK_ios_surface extension commands
#ifdef VK_USE_PLATFORM_IOS_MVK
    .CreateIOSSurfaceMVK = terminator_CreateIOSSurfaceMVK,
#endif // VK_USE_PLATFORM_IOS_MVK

    // ---- VK_MVK_macos_surface extension commands
#ifdef VK_USE_PLATFORM_MACOS_MVK
    .CreateMacOSSurfaceMVK = terminator_CreateMacOSSurfaceMVK,
#endif // VK_USE_PLATFORM_MACOS_MVK

    // ---- VK_EXT_sample_locations extension commands
    .GetPhysicalDeviceMultisamplePropertiesEXT = terminator_GetPhysicalDeviceMultisamplePropertiesEXT,
};

// A null-terminated list of all of the instance extensions supported by the loader.
// If an instance extension name is not in this list, but it is exported by one or more of the
// ICDs detected by the loader, then the extension name not in the list will be filtered out
// before passing the list of extensions to the application.
const char *const LOADER_INSTANCE_EXTENSIONS[] = {
                                                  VK_KHR_SURFACE_EXTENSION_NAME,
                                                  VK_KHR_DISPLAY_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_XLIB_KHR
                                                  VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
                                                  VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
                                                  VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
                                                  VK_KHR_MIR_SURFACE_EXTENSION_NAME,
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_WIN32_KHR
                                                  VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif // VK_USE_PLATFORM_WIN32_KHR
                                                  VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
                                                  VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
                                                  VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
                                                  VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,
                                                  VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
                                                  VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
                                                  VK_NV_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
                                                  VK_EXT_VALIDATION_FLAGS_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_VI_NN
                                                  VK_NN_VI_SURFACE_EXTENSION_NAME,
#endif // VK_USE_PLATFORM_VI_NN
                                                  VK_KHX_DEVICE_GROUP_CREATION_EXTENSION_NAME,
                                                  VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_XLIB_XRANDR_EXT
                                                  VK_EXT_ACQUIRE_XLIB_DISPLAY_EXTENSION_NAME,
#endif // VK_USE_PLATFORM_XLIB_XRANDR_EXT
                                                  VK_EXT_DISPLAY_SURFACE_COUNTER_EXTENSION_NAME,
                                                  VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_IOS_MVK
                                                  VK_MVK_IOS_SURFACE_EXTENSION_NAME,
#endif // VK_USE_PLATFORM_IOS_MVK
#ifdef VK_USE_PLATFORM_MACOS_MVK
                                                  VK_MVK_MACOS_SURFACE_EXTENSION_NAME,
#endif // VK_USE_PLATFORM_MACOS_MVK
                                                  NULL };

