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
 * Author: Ian Elliott <ian@lunarg.com>
 * Author: Jon Ashburn <jon@lunarg.com>
 * Author: Ian Elliott <ianelliott@google.com>
 * Author: Mark Lobodzinski <mark@lunarg.com>
 */

//#define _ISOC11_SOURCE /* for aligned_alloc() */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vk_loader_platform.h"
#include "loader.h"
#include "wsi.h"
#include <vulkan/vk_icd.h>

static const VkExtensionProperties wsi_surface_extension_info = {
    .extensionName = VK_KHR_SURFACE_EXTENSION_NAME,
    .specVersion = VK_KHR_SURFACE_SPEC_VERSION,
};

#ifdef VK_USE_PLATFORM_WIN32_KHR
static const VkExtensionProperties wsi_win32_surface_extension_info = {
    .extensionName = VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    .specVersion = VK_KHR_WIN32_SURFACE_SPEC_VERSION,
};
#endif // VK_USE_PLATFORM_WIN32_KHR

#ifdef VK_USE_PLATFORM_MIR_KHR
static const VkExtensionProperties wsi_mir_surface_extension_info = {
    .extensionName = VK_KHR_MIR_SURFACE_EXTENSION_NAME,
    .specVersion = VK_KHR_MIR_SURFACE_SPEC_VERSION,
};
#endif // VK_USE_PLATFORM_MIR_KHR

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
static const VkExtensionProperties wsi_wayland_surface_extension_info = {
    .extensionName = VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
    .specVersion = VK_KHR_WAYLAND_SURFACE_SPEC_VERSION,
};
#endif // VK_USE_PLATFORM_WAYLAND_KHR

#ifdef VK_USE_PLATFORM_XCB_KHR
static const VkExtensionProperties wsi_xcb_surface_extension_info = {
    .extensionName = VK_KHR_XCB_SURFACE_EXTENSION_NAME,
    .specVersion = VK_KHR_XCB_SURFACE_SPEC_VERSION,
};
#endif // VK_USE_PLATFORM_XCB_KHR

#ifdef VK_USE_PLATFORM_XLIB_KHR
static const VkExtensionProperties wsi_xlib_surface_extension_info = {
    .extensionName = VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
    .specVersion = VK_KHR_XLIB_SURFACE_SPEC_VERSION,
};
#endif // VK_USE_PLATFORM_XLIB_KHR

#ifdef VK_USE_PLATFORM_ANDROID_KHR
static const VkExtensionProperties wsi_android_surface_extension_info = {
    .extensionName = VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    .specVersion = VK_KHR_ANDROID_SURFACE_REVISION,
};
#endif // VK_USE_PLATFORM_ANDROID_KHR

void wsi_create_instance(struct loader_instance *ptr_instance,
                         const VkInstanceCreateInfo *pCreateInfo) {
    ptr_instance->wsi_surface_enabled = false;

#ifdef VK_USE_PLATFORM_WIN32_KHR
    ptr_instance->wsi_win32_surface_enabled = false;
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
    ptr_instance->wsi_mir_surface_enabled = false;
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    ptr_instance->wsi_wayland_surface_enabled = false;
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    ptr_instance->wsi_xcb_surface_enabled = false;
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    ptr_instance->wsi_xlib_surface_enabled = false;
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
    ptr_instance->wsi_android_surface_enabled = false;
#endif // VK_USE_PLATFORM_ANDROID_KHR

    ptr_instance->wsi_display_enabled = false;

    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                   VK_KHR_SURFACE_EXTENSION_NAME) == 0) {
            ptr_instance->wsi_surface_enabled = true;
            continue;
        }
#ifdef VK_USE_PLATFORM_WIN32_KHR
        if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                   VK_KHR_WIN32_SURFACE_EXTENSION_NAME) == 0) {
            ptr_instance->wsi_win32_surface_enabled = true;
            continue;
        }
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
        if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                   VK_KHR_MIR_SURFACE_EXTENSION_NAME) == 0) {
            ptr_instance->wsi_mir_surface_enabled = true;
            continue;
        }
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
        if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                   VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME) == 0) {
            ptr_instance->wsi_wayland_surface_enabled = true;
            continue;
        }
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
        if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                   VK_KHR_XCB_SURFACE_EXTENSION_NAME) == 0) {
            ptr_instance->wsi_xcb_surface_enabled = true;
            continue;
        }
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
        if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                   VK_KHR_XLIB_SURFACE_EXTENSION_NAME) == 0) {
            ptr_instance->wsi_xlib_surface_enabled = true;
            continue;
        }
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
        if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                   VK_KHR_ANDROID_SURFACE_EXTENSION_NAME) == 0) {
            ptr_instance->wsi_android_surface_enabled = true;
            continue;
        }
#endif // VK_USE_PLATFORM_ANDROID_KHR
        if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                   VK_KHR_DISPLAY_EXTENSION_NAME) == 0) {
            ptr_instance->wsi_display_enabled = true;
            continue;
        }
    }
}
/*
 * Linux WSI surface extensions are not always compiled into the loader. (Assume
 * for Windows the KHR_win32_surface is always compiled into loader). A given
 * Linux build environment might not have the headers required for building one
 * of the four extensions  (Xlib, Xcb, Mir, Wayland).  Thus, need to check if
 * the built loader actually supports the particular Linux surface extension.
 * If not supported by the built loader it will not be included in the list of
 * enumerated instance extensions.  This solves the issue where an ICD or layer
 * advertises support for a given Linux surface extension but the loader was not
 * built to support the extension. */
bool wsi_unsupported_instance_extension(const VkExtensionProperties *ext_prop) {
#ifndef VK_USE_PLATFORM_MIR_KHR
    if (!strcmp(ext_prop->extensionName, "VK_KHR_mir_surface"))
        return true;
#endif // VK_USE_PLATFORM_MIR_KHR
#ifndef VK_USE_PLATFORM_WAYLAND_KHR
    if (!strcmp(ext_prop->extensionName, "VK_KHR_wayland_surface"))
        return true;
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifndef VK_USE_PLATFORM_XCB_KHR
    if (!strcmp(ext_prop->extensionName, "VK_KHR_xcb_surface"))
        return true;
#endif // VK_USE_PLATFORM_XCB_KHR
#ifndef VK_USE_PLATFORM_XLIB_KHR
    if (!strcmp(ext_prop->extensionName, "VK_KHR_xlib_surface"))
        return true;
#endif // VK_USE_PLATFORM_XLIB_KHR

    return false;
}
/*
 * Functions for the VK_KHR_surface extension:
 */

/*
 * This is the trampoline entrypoint
 * for DestroySurfaceKHR
 */
LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                    const VkAllocationCallbacks *pAllocator) {
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(instance);
    disp->DestroySurfaceKHR(instance, surface, pAllocator);
}

// TODO probably need to lock around all the loader_get_instance() calls.
/*
 * This is the instance chain terminator function
 * for DestroySurfaceKHR
 */
VKAPI_ATTR void VKAPI_CALL
terminator_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                             const VkAllocationCallbacks *pAllocator) {
    struct loader_instance *ptr_instance = loader_get_instance(instance);

    loader_heap_free(ptr_instance, (void *)surface);
}

/*
 * This is the trampoline entrypoint
 * for GetPhysicalDeviceSurfaceSupportKHR
 */
LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice,
                                     uint32_t queueFamilyIndex,
                                     VkSurfaceKHR surface,
                                     VkBool32 *pSupported) {
    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_dispatch(physicalDevice);
    VkResult res = disp->GetPhysicalDeviceSurfaceSupportKHR(
        unwrapped_phys_dev, queueFamilyIndex, surface, pSupported);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceSurfaceSupportKHR
 */
VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice,
                                              uint32_t queueFamilyIndex,
                                              VkSurfaceKHR surface,
                                              VkBool32 *pSupported) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_instance *ptr_instance =
        (struct loader_instance *)phys_dev->this_icd->this_instance;
    if (!ptr_instance->wsi_surface_enabled) {
        loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "VK_KHR_VK_KHR_surface extension not enabled.  "
                   "vkGetPhysicalDeviceSurfaceSupportKHR not executed!\n");
        return VK_SUCCESS;
    }

    // Next, if so, proceed with the implementation of this function:
    struct loader_icd *icd = phys_dev->this_icd;

    assert(pSupported &&
           "GetPhysicalDeviceSurfaceSupportKHR: Error, null pSupported");
    *pSupported = false;

    assert(icd->GetPhysicalDeviceSurfaceSupportKHR &&
           "loader: null GetPhysicalDeviceSurfaceSupportKHR ICD pointer");

    return icd->GetPhysicalDeviceSurfaceSupportKHR(
        phys_dev->phys_dev, queueFamilyIndex, surface, pSupported);
}

/*
 * This is the trampoline entrypoint
 * for GetPhysicalDeviceSurfaceCapabilitiesKHR
 */
LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR *pSurfaceCapabilities) {

    const VkLayerInstanceDispatchTable *disp;
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    disp = loader_get_instance_dispatch(physicalDevice);
    VkResult res = disp->GetPhysicalDeviceSurfaceCapabilitiesKHR(
        unwrapped_phys_dev, surface, pSurfaceCapabilities);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceSurfaceCapabilitiesKHR
 */
VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR *pSurfaceCapabilities) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_instance *ptr_instance =
        (struct loader_instance *)phys_dev->this_icd->this_instance;
    if (!ptr_instance->wsi_surface_enabled) {
        loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "VK_KHR_surface extension not enabled.  "
                   "vkGetPhysicalDeviceSurfaceCapabilitiesKHR not executed!\n");
        return VK_SUCCESS;
    }

    // Next, if so, proceed with the implementation of this function:
    struct loader_icd *icd = phys_dev->this_icd;

    assert(pSurfaceCapabilities && "GetPhysicalDeviceSurfaceCapabilitiesKHR: "
                                   "Error, null pSurfaceCapabilities");

    assert(icd->GetPhysicalDeviceSurfaceCapabilitiesKHR &&
           "loader: null GetPhysicalDeviceSurfaceCapabilitiesKHR ICD pointer");

    return icd->GetPhysicalDeviceSurfaceCapabilitiesKHR(
        phys_dev->phys_dev, surface, pSurfaceCapabilities);
}

/*
 * This is the trampoline entrypoint
 * for GetPhysicalDeviceSurfaceFormatsKHR
 */
LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice,
                                     VkSurfaceKHR surface,
                                     uint32_t *pSurfaceFormatCount,
                                     VkSurfaceFormatKHR *pSurfaceFormats) {
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkResult res = disp->GetPhysicalDeviceSurfaceFormatsKHR(
        unwrapped_phys_dev, surface, pSurfaceFormatCount, pSurfaceFormats);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceSurfaceFormatsKHR
 */
VKAPI_ATTR VkResult VKAPI_CALL terminator_GetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t *pSurfaceFormatCount, VkSurfaceFormatKHR *pSurfaceFormats) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_instance *ptr_instance =
        (struct loader_instance *)phys_dev->this_icd->this_instance;
    if (!ptr_instance->wsi_surface_enabled) {
        loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "VK_KHR_surface extension not enabled.  "
                   "vkGetPhysicalDeviceSurfaceFormatsKHR not executed!\n");
        return VK_SUCCESS;
    }

    // Next, if so, proceed with the implementation of this function:
    struct loader_icd *icd = phys_dev->this_icd;

    assert(
        pSurfaceFormatCount &&
        "GetPhysicalDeviceSurfaceFormatsKHR: Error, null pSurfaceFormatCount");

    assert(icd->GetPhysicalDeviceSurfaceFormatsKHR &&
           "loader: null GetPhysicalDeviceSurfaceFormatsKHR ICD pointer");

    return icd->GetPhysicalDeviceSurfaceFormatsKHR(
        phys_dev->phys_dev, surface, pSurfaceFormatCount, pSurfaceFormats);
}

/*
 * This is the trampoline entrypoint
 * for GetPhysicalDeviceSurfacePresentModesKHR
 */
LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice,
                                          VkSurfaceKHR surface,
                                          uint32_t *pPresentModeCount,
                                          VkPresentModeKHR *pPresentModes) {
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkResult res = disp->GetPhysicalDeviceSurfacePresentModesKHR(
        unwrapped_phys_dev, surface, pPresentModeCount, pPresentModes);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceSurfacePresentModesKHR
 */
VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_instance *ptr_instance =
        (struct loader_instance *)phys_dev->this_icd->this_instance;
    if (!ptr_instance->wsi_surface_enabled) {
        loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "VK_KHR_surface extension not enabled.  "
                   "vkGetPhysicalDeviceSurfacePresentModesKHR not executed!\n");
        return VK_SUCCESS;
    }

    // Next, if so, proceed with the implementation of this function:
    struct loader_icd *icd = phys_dev->this_icd;

    assert(pPresentModeCount && "GetPhysicalDeviceSurfacePresentModesKHR: "
                                "Error, null pPresentModeCount");

    assert(icd->GetPhysicalDeviceSurfacePresentModesKHR &&
           "loader: null GetPhysicalDeviceSurfacePresentModesKHR ICD pointer");

    return icd->GetPhysicalDeviceSurfacePresentModesKHR(
        phys_dev->phys_dev, surface, pPresentModeCount, pPresentModes);
}

/*
 * Functions for the VK_KHR_swapchain extension:
 */

/*
 * This is the trampoline entrypoint
 * for CreateSwapchainKHR
 */
LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSwapchainKHR(VkDevice device,
                     const VkSwapchainCreateInfoKHR *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkSwapchainKHR *pSwapchain) {
    const VkLayerDispatchTable *disp;
    disp = loader_get_dispatch(device);
    VkResult res =
        disp->CreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
    return res;
}

/*
 * This is the trampoline entrypoint
 * for DestroySwapchainKHR
 */
LOADER_EXPORT VKAPI_ATTR void VKAPI_CALL
vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                      const VkAllocationCallbacks *pAllocator) {
    const VkLayerDispatchTable *disp;
    disp = loader_get_dispatch(device);
    disp->DestroySwapchainKHR(device, swapchain, pAllocator);
}

/*
 * This is the trampoline entrypoint
 * for GetSwapchainImagesKHR
 */
LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain,
                        uint32_t *pSwapchainImageCount,
                        VkImage *pSwapchainImages) {
    const VkLayerDispatchTable *disp;
    disp = loader_get_dispatch(device);
    VkResult res = disp->GetSwapchainImagesKHR(
        device, swapchain, pSwapchainImageCount, pSwapchainImages);
    return res;
}

/*
 * This is the trampoline entrypoint
 * for AcquireNextImageKHR
 */
LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain,
                      uint64_t timeout, VkSemaphore semaphore, VkFence fence,
                      uint32_t *pImageIndex) {
    const VkLayerDispatchTable *disp;
    disp = loader_get_dispatch(device);
    VkResult res = disp->AcquireNextImageKHR(device, swapchain, timeout,
                                             semaphore, fence, pImageIndex);
    return res;
}

/*
 * This is the trampoline entrypoint
 * for QueuePresentKHR
 */
LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *pPresentInfo) {
    const VkLayerDispatchTable *disp;
    disp = loader_get_dispatch(queue);
    VkResult res = disp->QueuePresentKHR(queue, pPresentInfo);
    return res;
}

#ifdef VK_USE_PLATFORM_WIN32_KHR

/*
 * Functions for the VK_KHR_win32_surface extension:
 */

/*
 * This is the trampoline entrypoint
 * for CreateWin32SurfaceKHR
 */
LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateWin32SurfaceKHR(VkInstance instance,
                        const VkWin32SurfaceCreateInfoKHR *pCreateInfo,
                        const VkAllocationCallbacks *pAllocator,
                        VkSurfaceKHR *pSurface) {
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(instance);
    VkResult res;

    res = disp->CreateWin32SurfaceKHR(instance, pCreateInfo, pAllocator,
                                      pSurface);
    return res;
}

/*
 * This is the instance chain terminator function
 * for CreateWin32SurfaceKHR
 */
VKAPI_ATTR VkResult VKAPI_CALL
terminator_CreateWin32SurfaceKHR(VkInstance instance,
                                 const VkWin32SurfaceCreateInfoKHR *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator,
                                 VkSurfaceKHR *pSurface) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_instance *ptr_instance = loader_get_instance(instance);
    if (!ptr_instance->wsi_win32_surface_enabled) {
        loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "VK_KHR_win32_surface extension not enabled.  "
                   "vkCreateWin32SurfaceKHR not executed!\n");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    // Next, if so, proceed with the implementation of this function:
    VkIcdSurfaceWin32 *pIcdSurface = NULL;

    pIcdSurface = loader_heap_alloc(ptr_instance, sizeof(VkIcdSurfaceWin32),
                                    VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (pIcdSurface == NULL) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    pIcdSurface->base.platform = VK_ICD_WSI_PLATFORM_WIN32;
    pIcdSurface->hinstance = pCreateInfo->hinstance;
    pIcdSurface->hwnd = pCreateInfo->hwnd;

    *pSurface = (VkSurfaceKHR)pIcdSurface;

    return VK_SUCCESS;
}

/*
 * This is the trampoline entrypoint
 * for GetPhysicalDeviceWin32PresentationSupportKHR
 */
LOADER_EXPORT VKAPI_ATTR VkBool32 VKAPI_CALL
vkGetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice physicalDevice,
                                               uint32_t queueFamilyIndex) {
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkBool32 res = disp->GetPhysicalDeviceWin32PresentationSupportKHR(
        unwrapped_phys_dev, queueFamilyIndex);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceWin32PresentationSupportKHR
 */
VKAPI_ATTR VkBool32 VKAPI_CALL
terminator_GetPhysicalDeviceWin32PresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_instance *ptr_instance =
        (struct loader_instance *)phys_dev->this_icd->this_instance;
    if (!ptr_instance->wsi_win32_surface_enabled) {
        loader_log(
            ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
            "VK_KHR_win32_surface extension not enabled.  "
            "vkGetPhysicalDeviceWin32PresentationSupportKHR not executed!\n");
        return VK_SUCCESS;
    }

    // Next, if so, proceed with the implementation of this function:
    struct loader_icd *icd = phys_dev->this_icd;

    assert(icd->GetPhysicalDeviceWin32PresentationSupportKHR &&
           "loader: null GetPhysicalDeviceWin32PresentationSupportKHR ICD "
           "pointer");

    return icd->GetPhysicalDeviceWin32PresentationSupportKHR(phys_dev->phys_dev,
                                                             queueFamilyIndex);
}
#endif // VK_USE_PLATFORM_WIN32_KHR

#ifdef VK_USE_PLATFORM_MIR_KHR

/*
 * Functions for the VK_KHR_mir_surface extension:
 */

/*
 * This is the trampoline entrypoint
 * for CreateMirSurfaceKHR
 */
LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateMirSurfaceKHR(VkInstance instance,
                      const VkMirSurfaceCreateInfoKHR *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkSurfaceKHR *pSurface) {
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(instance);
    VkResult res;

    res =
        disp->CreateMirSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
    return res;
}

/*
 * This is the instance chain terminator function
 * for CreateMirSurfaceKHR
 */
VKAPI_ATTR VkResult VKAPI_CALL
terminator_CreateMirSurfaceKHR(VkInstance instance,
                               const VkMirSurfaceCreateInfoKHR *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkSurfaceKHR *pSurface) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_instance *ptr_instance = loader_get_instance(instance);
    if (!ptr_instance->wsi_mir_surface_enabled) {
        loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "VK_KHR_mir_surface extension not enabled.  "
                   "vkCreateMirSurfaceKHR not executed!\n");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    // Next, if so, proceed with the implementation of this function:
    VkIcdSurfaceMir *pIcdSurface = NULL;

    pIcdSurface = loader_heap_alloc(ptr_instance, sizeof(VkIcdSurfaceMir),
                                    VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (pIcdSurface == NULL) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    pIcdSurface->base.platform = VK_ICD_WSI_PLATFORM_MIR;
    pIcdSurface->connection = pCreateInfo->connection;
    pIcdSurface->mirSurface = pCreateInfo->mirSurface;

    *pSurface = (VkSurfaceKHR)pIcdSurface;

    return VK_SUCCESS;
}

/*
 * This is the trampoline entrypoint
 * for GetPhysicalDeviceMirPresentationSupportKHR
 */
LOADER_EXPORT VKAPI_ATTR VkBool32 VKAPI_CALL
vkGetPhysicalDeviceMirPresentationSupportKHR(VkPhysicalDevice physicalDevice,
                                             uint32_t queueFamilyIndex,
                                             MirConnection *connection) {
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkBool32 res = disp->GetPhysicalDeviceMirPresentationSupportKHR(
        unwrapped_phys_dev, queueFamilyIndex, connection);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceMirPresentationSupportKHR
 */
VKAPI_ATTR VkBool32 VKAPI_CALL
terminator_GetPhysicalDeviceMirPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    MirConnection *connection) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_instance *ptr_instance =
        (struct loader_instance *)phys_dev->this_icd->this_instance;
    if (!ptr_instance->wsi_mir_surface_enabled) {
        loader_log(
            ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
            "VK_KHR_mir_surface extension not enabled.  "
            "vkGetPhysicalDeviceMirPresentationSupportKHR not executed!\n");
        return VK_SUCCESS;
    }

    // Next, if so, proceed with the implementation of this function:
    struct loader_icd *icd = phys_dev->this_icd;

    assert(
        icd->GetPhysicalDeviceMirPresentationSupportKHR &&
        "loader: null GetPhysicalDeviceMirPresentationSupportKHR ICD pointer");

    return icd->GetPhysicalDeviceMirPresentationSupportKHR(
        phys_dev->phys_dev, queueFamilyIndex, connection);
}
#endif // VK_USE_PLATFORM_MIR_KHR

#ifdef VK_USE_PLATFORM_WAYLAND_KHR

/*
 * Functions for the VK_KHR_wayland_surface extension:
 */

/*
 * This is the trampoline entrypoint
 * for CreateWaylandSurfaceKHR
 */
LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateWaylandSurfaceKHR(VkInstance instance,
                          const VkWaylandSurfaceCreateInfoKHR *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator,
                          VkSurfaceKHR *pSurface) {
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(instance);
    VkResult res;

    res = disp->CreateWaylandSurfaceKHR(instance, pCreateInfo, pAllocator,
                                        pSurface);
    return res;
}

/*
 * This is the instance chain terminator function
 * for CreateWaylandSurfaceKHR
 */
VKAPI_ATTR VkResult VKAPI_CALL
terminator_CreateWaylandSurfaceKHR(
    VkInstance instance, const VkWaylandSurfaceCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_instance *ptr_instance = loader_get_instance(instance);
    if (!ptr_instance->wsi_wayland_surface_enabled) {
        loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "VK_KHR_wayland_surface extension not enabled.  "
                   "vkCreateWaylandSurfaceKHR not executed!\n");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    // Next, if so, proceed with the implementation of this function:
    VkIcdSurfaceWayland *pIcdSurface = NULL;

    pIcdSurface = loader_heap_alloc(ptr_instance, sizeof(VkIcdSurfaceWayland),
                                    VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (pIcdSurface == NULL) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    pIcdSurface->base.platform = VK_ICD_WSI_PLATFORM_WAYLAND;
    pIcdSurface->display = pCreateInfo->display;
    pIcdSurface->surface = pCreateInfo->surface;

    *pSurface = (VkSurfaceKHR)pIcdSurface;

    return VK_SUCCESS;
}

/*
 * This is the trampoline entrypoint
 * for GetPhysicalDeviceWaylandPresentationSupportKHR
 */
LOADER_EXPORT VKAPI_ATTR VkBool32 VKAPI_CALL
vkGetPhysicalDeviceWaylandPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    struct wl_display *display) {
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkBool32 res = disp->GetPhysicalDeviceWaylandPresentationSupportKHR(
        unwrapped_phys_dev, queueFamilyIndex, display);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceWaylandPresentationSupportKHR
 */
VKAPI_ATTR VkBool32 VKAPI_CALL
terminator_GetPhysicalDeviceWaylandPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    struct wl_display *display) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_instance *ptr_instance =
        (struct loader_instance *)phys_dev->this_icd->this_instance;
    if (!ptr_instance->wsi_wayland_surface_enabled) {
        loader_log(
            ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
            "VK_KHR_wayland_surface extension not enabled.  "
            "vkGetPhysicalDeviceWaylandPresentationSupportKHR not executed!\n");
        return VK_SUCCESS;
    }

    // Next, if so, proceed with the implementation of this function:
    struct loader_icd *icd = phys_dev->this_icd;

    assert(icd->GetPhysicalDeviceWaylandPresentationSupportKHR &&
           "loader: null GetPhysicalDeviceWaylandPresentationSupportKHR ICD "
           "pointer");

    return icd->GetPhysicalDeviceWaylandPresentationSupportKHR(
        phys_dev->phys_dev, queueFamilyIndex, display);
}
#endif // VK_USE_PLATFORM_WAYLAND_KHR

#ifdef VK_USE_PLATFORM_XCB_KHR

/*
 * Functions for the VK_KHR_xcb_surface extension:
 */

/*
 * This is the trampoline entrypoint
 * for CreateXcbSurfaceKHR
 */
LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateXcbSurfaceKHR(VkInstance instance,
                      const VkXcbSurfaceCreateInfoKHR *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkSurfaceKHR *pSurface) {
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(instance);
    VkResult res;

    res =
        disp->CreateXcbSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
    return res;
}

/*
 * This is the instance chain terminator function
 * for CreateXcbSurfaceKHR
 */
VKAPI_ATTR VkResult VKAPI_CALL
terminator_CreateXcbSurfaceKHR(VkInstance instance,
                               const VkXcbSurfaceCreateInfoKHR *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkSurfaceKHR *pSurface) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_instance *ptr_instance = loader_get_instance(instance);
    if (!ptr_instance->wsi_xcb_surface_enabled) {
        loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "VK_KHR_xcb_surface extension not enabled.  "
                   "vkCreateXcbSurfaceKHR not executed!\n");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    // Next, if so, proceed with the implementation of this function:
    VkIcdSurfaceXcb *pIcdSurface = NULL;

    pIcdSurface = loader_heap_alloc(ptr_instance, sizeof(VkIcdSurfaceXcb),
                                    VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (pIcdSurface == NULL) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    pIcdSurface->base.platform = VK_ICD_WSI_PLATFORM_XCB;
    pIcdSurface->connection = pCreateInfo->connection;
    pIcdSurface->window = pCreateInfo->window;

    *pSurface = (VkSurfaceKHR)pIcdSurface;

    return VK_SUCCESS;
}

/*
 * This is the trampoline entrypoint
 * for GetPhysicalDeviceXcbPresentationSupportKHR
 */
LOADER_EXPORT VKAPI_ATTR VkBool32 VKAPI_CALL
vkGetPhysicalDeviceXcbPresentationSupportKHR(VkPhysicalDevice physicalDevice,
                                             uint32_t queueFamilyIndex,
                                             xcb_connection_t *connection,
                                             xcb_visualid_t visual_id) {
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkBool32 res = disp->GetPhysicalDeviceXcbPresentationSupportKHR(
        unwrapped_phys_dev, queueFamilyIndex, connection, visual_id);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceXcbPresentationSupportKHR
 */
VKAPI_ATTR VkBool32 VKAPI_CALL
terminator_GetPhysicalDeviceXcbPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    xcb_connection_t *connection, xcb_visualid_t visual_id) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_instance *ptr_instance =
        (struct loader_instance *)phys_dev->this_icd->this_instance;
    if (!ptr_instance->wsi_xcb_surface_enabled) {
        loader_log(
            ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
            "VK_KHR_xcb_surface extension not enabled.  "
            "vkGetPhysicalDeviceXcbPresentationSupportKHR not executed!\n");
        return VK_SUCCESS;
    }

    // Next, if so, proceed with the implementation of this function:
    struct loader_icd *icd = phys_dev->this_icd;

    assert(
        icd->GetPhysicalDeviceXcbPresentationSupportKHR &&
        "loader: null GetPhysicalDeviceXcbPresentationSupportKHR ICD pointer");

    return icd->GetPhysicalDeviceXcbPresentationSupportKHR(
        phys_dev->phys_dev, queueFamilyIndex, connection, visual_id);
}
#endif // VK_USE_PLATFORM_XCB_KHR

#ifdef VK_USE_PLATFORM_XLIB_KHR

/*
 * Functions for the VK_KHR_xlib_surface extension:
 */

/*
 * This is the trampoline entrypoint
 * for CreateXlibSurfaceKHR
 */
LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateXlibSurfaceKHR(VkInstance instance,
                       const VkXlibSurfaceCreateInfoKHR *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkSurfaceKHR *pSurface) {
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(instance);
    VkResult res;

    res =
        disp->CreateXlibSurfaceKHR(instance, pCreateInfo, pAllocator, pSurface);
    return res;
}

/*
 * This is the instance chain terminator function
 * for CreateXlibSurfaceKHR
 */
VKAPI_ATTR VkResult VKAPI_CALL
terminator_CreateXlibSurfaceKHR(VkInstance instance,
                                const VkXlibSurfaceCreateInfoKHR *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkSurfaceKHR *pSurface) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_instance *ptr_instance = loader_get_instance(instance);
    if (!ptr_instance->wsi_xlib_surface_enabled) {
        loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "VK_KHR_xlib_surface extension not enabled.  "
                   "vkCreateXlibSurfaceKHR not executed!\n");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    // Next, if so, proceed with the implementation of this function:
    VkIcdSurfaceXlib *pIcdSurface = NULL;

    pIcdSurface = loader_heap_alloc(ptr_instance, sizeof(VkIcdSurfaceXlib),
                                    VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (pIcdSurface == NULL) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    pIcdSurface->base.platform = VK_ICD_WSI_PLATFORM_XLIB;
    pIcdSurface->dpy = pCreateInfo->dpy;
    pIcdSurface->window = pCreateInfo->window;

    *pSurface = (VkSurfaceKHR)pIcdSurface;

    return VK_SUCCESS;
}

/*
 * This is the trampoline entrypoint
 * for GetPhysicalDeviceXlibPresentationSupportKHR
 */
LOADER_EXPORT VKAPI_ATTR VkBool32 VKAPI_CALL
vkGetPhysicalDeviceXlibPresentationSupportKHR(VkPhysicalDevice physicalDevice,
                                              uint32_t queueFamilyIndex,
                                              Display *dpy, VisualID visualID) {
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkBool32 res = disp->GetPhysicalDeviceXlibPresentationSupportKHR(
        unwrapped_phys_dev, queueFamilyIndex, dpy, visualID);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceXlibPresentationSupportKHR
 */
VKAPI_ATTR VkBool32 VKAPI_CALL
terminator_GetPhysicalDeviceXlibPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, Display *dpy,
    VisualID visualID) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_instance *ptr_instance =
        (struct loader_instance *)phys_dev->this_icd->this_instance;
    if (!ptr_instance->wsi_xlib_surface_enabled) {
        loader_log(
            ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
            "VK_KHR_xlib_surface extension not enabled.  "
            "vkGetPhysicalDeviceXlibPresentationSupportKHR not executed!\n");
        return VK_SUCCESS;
    }

    // Next, if so, proceed with the implementation of this function:
    struct loader_icd *icd = phys_dev->this_icd;

    assert(
        icd->GetPhysicalDeviceXlibPresentationSupportKHR &&
        "loader: null GetPhysicalDeviceXlibPresentationSupportKHR ICD pointer");

    return icd->GetPhysicalDeviceXlibPresentationSupportKHR(
        phys_dev->phys_dev, queueFamilyIndex, dpy, visualID);
}
#endif // VK_USE_PLATFORM_XLIB_KHR

#ifdef VK_USE_PLATFORM_ANDROID_KHR

/*
 * Functions for the VK_KHR_android_surface extension:
 */

/*
 * This is the trampoline entrypoint
 * for CreateAndroidSurfaceKHR
 */
LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateAndroidSurfaceKHR(VkInstance instance, ANativeWindow *window,
                          const VkAllocationCallbacks *pAllocator,
                          VkSurfaceKHR *pSurface) {
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(instance);
    VkResult res;

    res = disp->CreateAndroidSurfaceKHR(instance, window, pAllocator, pSurface);
    return res;
}

/*
 * This is the instance chain terminator function
 * for CreateAndroidSurfaceKHR
 */
VKAPI_ATTR VkResult VKAPI_CALL
terminator_CreateAndroidSurfaceKHR(VkInstance instance, Window window,
                                   const VkAllocationCallbacks *pAllocator,
                                   VkSurfaceKHR *pSurface) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_instance *ptr_instance = loader_get_instance(instance);
    if (!ptr_instance->wsi_display_enabled) {
        loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "VK_KHR_display extension not enabled.  "
                   "vkCreateAndroidSurfaceKHR not executed!\n");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    // Next, if so, proceed with the implementation of this function:
    VkIcdSurfaceAndroid *pIcdSurface = NULL;

    pIcdSurface = loader_heap_alloc(ptr_instance, sizeof(VkIcdSurfaceAndroid),
                                    VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (pIcdSurface == NULL) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    pIcdSurface->base.platform = VK_ICD_WSI_PLATFORM_ANDROID;
    pIcdSurface->dpy = dpy;
    pIcdSurface->window = window;

    *pSurface = (VkSurfaceKHR)pIcdSurface;

    return VK_SUCCESS;
}

#endif // VK_USE_PLATFORM_ANDROID_KHR

/*
 * Functions for the VK_KHR_display instance extension:
 */
LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceDisplayPropertiesKHR(VkPhysicalDevice physicalDevice,
                                        uint32_t *pPropertyCount,
                                        VkDisplayPropertiesKHR *pProperties) {
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkResult res = disp->GetPhysicalDeviceDisplayPropertiesKHR(
        unwrapped_phys_dev, pPropertyCount, pProperties);
    return res;
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_GetPhysicalDeviceDisplayPropertiesKHR(
    VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
    VkDisplayPropertiesKHR *pProperties) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_instance *ptr_instance =
        (struct loader_instance *)phys_dev->this_icd->this_instance;
    if (!ptr_instance->wsi_display_enabled) {
        loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "VK_KHR_display extension not enabled.  "
                   "vkGetPhysicalDeviceDisplayPropertiesKHR not executed!\n");
        return VK_SUCCESS;
    }

    // Next, if so, proceed with the implementation of this function:
    struct loader_icd *icd = phys_dev->this_icd;

    assert(icd->GetPhysicalDeviceDisplayPropertiesKHR &&
           "loader: null GetPhysicalDeviceDisplayPropertiesKHR ICD pointer");

    return icd->GetPhysicalDeviceDisplayPropertiesKHR(
        phys_dev->phys_dev, pPropertyCount, pProperties);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetPhysicalDeviceDisplayPlanePropertiesKHR(
    VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
    VkDisplayPlanePropertiesKHR *pProperties) {
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkResult res = disp->GetPhysicalDeviceDisplayPlanePropertiesKHR(
        unwrapped_phys_dev, pPropertyCount, pProperties);
    return res;
}

VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetPhysicalDeviceDisplayPlanePropertiesKHR(
    VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
    VkDisplayPlanePropertiesKHR *pProperties) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_instance *ptr_instance =
        (struct loader_instance *)phys_dev->this_icd->this_instance;
    if (!ptr_instance->wsi_display_enabled) {
        loader_log(
            ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
            "VK_KHR_display extension not enabled.  "
            "vkGetPhysicalDeviceDisplayPlanePropertiesKHR not executed!\n");
        return VK_SUCCESS;
    }

    // Next, if so, proceed with the implementation of this function:
    struct loader_icd *icd = phys_dev->this_icd;

    assert(
        icd->GetPhysicalDeviceDisplayPlanePropertiesKHR &&
        "loader: null GetPhysicalDeviceDisplayPlanePropertiesKHR ICD pointer");

    return icd->GetPhysicalDeviceDisplayPlanePropertiesKHR(
        phys_dev->phys_dev, pPropertyCount, pProperties);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physicalDevice,
                                      uint32_t planeIndex,
                                      uint32_t *pDisplayCount,
                                      VkDisplayKHR *pDisplays) {
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkResult res = disp->GetDisplayPlaneSupportedDisplaysKHR(
        unwrapped_phys_dev, planeIndex, pDisplayCount, pDisplays);
    return res;
}

VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physicalDevice,
                                               uint32_t planeIndex,
                                               uint32_t *pDisplayCount,
                                               VkDisplayKHR *pDisplays) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_instance *ptr_instance =
        (struct loader_instance *)phys_dev->this_icd->this_instance;
    if (!ptr_instance->wsi_display_enabled) {
        loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "VK_KHR_display extension not enabled.  "
                   "vkGetDisplayPlaneSupportedDisplaysKHR not executed!\n");
        return VK_SUCCESS;
    }

    // Next, if so, proceed with the implementation of this function:
    struct loader_icd *icd = phys_dev->this_icd;

    assert(icd->GetDisplayPlaneSupportedDisplaysKHR &&
           "loader: null GetDisplayPlaneSupportedDisplaysKHR ICD pointer");

    return icd->GetDisplayPlaneSupportedDisplaysKHR(
        phys_dev->phys_dev, planeIndex, pDisplayCount, pDisplays);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetDisplayModePropertiesKHR(VkPhysicalDevice physicalDevice,
                              VkDisplayKHR display, uint32_t *pPropertyCount,
                              VkDisplayModePropertiesKHR *pProperties) {
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkResult res = disp->GetDisplayModePropertiesKHR(
        unwrapped_phys_dev, display, pPropertyCount, pProperties);
    return res;
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_GetDisplayModePropertiesKHR(
    VkPhysicalDevice physicalDevice, VkDisplayKHR display,
    uint32_t *pPropertyCount, VkDisplayModePropertiesKHR *pProperties) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_instance *ptr_instance =
        (struct loader_instance *)phys_dev->this_icd->this_instance;
    if (!ptr_instance->wsi_display_enabled) {
        loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "VK_KHR_display extension not enabled.  "
                   "vkGetDisplayModePropertiesKHR not executed!\n");
        return VK_SUCCESS;
    }

    // Next, if so, proceed with the implementation of this function:
    struct loader_icd *icd = phys_dev->this_icd;

    assert(icd->GetDisplayModePropertiesKHR &&
           "loader: null GetDisplayModePropertiesKHR ICD pointer");

    return icd->GetDisplayModePropertiesKHR(phys_dev->phys_dev, display,
                                            pPropertyCount, pProperties);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDisplayModeKHR(VkPhysicalDevice physicalDevice, VkDisplayKHR display,
                       const VkDisplayModeCreateInfoKHR *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkDisplayModeKHR *pMode) {
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkResult res = disp->CreateDisplayModeKHR(unwrapped_phys_dev, display,
                                              pCreateInfo, pAllocator, pMode);
    return res;
}

VKAPI_ATTR VkResult VKAPI_CALL
terminator_CreateDisplayModeKHR(VkPhysicalDevice physicalDevice,
                                VkDisplayKHR display,
                                const VkDisplayModeCreateInfoKHR *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkDisplayModeKHR *pMode) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_instance *ptr_instance =
        (struct loader_instance *)phys_dev->this_icd->this_instance;
    if (!ptr_instance->wsi_display_enabled) {
        loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "VK_KHR_display extension not enabled.  "
                   "vkCreateDisplayModeKHR not executed!\n");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    // Next, if so, proceed with the implementation of this function:
    struct loader_icd *icd = phys_dev->this_icd;

    assert(icd->CreateDisplayModeKHR &&
           "loader: null CreateDisplayModeKHR ICD pointer");

    return icd->CreateDisplayModeKHR(phys_dev->phys_dev, display, pCreateInfo,
                                     pAllocator, pMode);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkGetDisplayPlaneCapabilitiesKHR(VkPhysicalDevice physicalDevice,
                                 VkDisplayModeKHR mode, uint32_t planeIndex,
                                 VkDisplayPlaneCapabilitiesKHR *pCapabilities) {
    VkPhysicalDevice unwrapped_phys_dev =
        loader_unwrap_physical_device(physicalDevice);
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkResult res = disp->GetDisplayPlaneCapabilitiesKHR(
        unwrapped_phys_dev, mode, planeIndex, pCapabilities);
    return res;
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_GetDisplayPlaneCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkDisplayModeKHR mode, uint32_t planeIndex,
    VkDisplayPlaneCapabilitiesKHR *pCapabilities) {
    // First, check to ensure the appropriate extension was enabled:
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
    struct loader_instance *ptr_instance =
        (struct loader_instance *)phys_dev->this_icd->this_instance;
    if (!ptr_instance->wsi_display_enabled) {
        loader_log(ptr_instance, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "VK_KHR_display extension not enabled.  "
                   "vkGetDisplayPlaneCapabilitiesKHR not executed!\n");
        return VK_SUCCESS;
    }

    // Next, if so, proceed with the implementation of this function:
    struct loader_icd *icd = phys_dev->this_icd;

    assert(icd->GetDisplayPlaneCapabilitiesKHR &&
           "loader: null GetDisplayPlaneCapabilitiesKHR ICD pointer");

    return icd->GetDisplayPlaneCapabilitiesKHR(phys_dev->phys_dev, mode,
                                               planeIndex, pCapabilities);
}

LOADER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkCreateDisplayPlaneSurfaceKHR(VkInstance instance,
                               const VkDisplaySurfaceCreateInfoKHR *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkSurfaceKHR *pSurface) {
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(instance);
    VkResult res;

    res = disp->CreateDisplayPlaneSurfaceKHR(instance, pCreateInfo, pAllocator,
                                             pSurface);
    return res;
}

VKAPI_ATTR VkResult VKAPI_CALL terminator_CreateDisplayPlaneSurfaceKHR(
    VkInstance instance, const VkDisplaySurfaceCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface) {
    struct loader_instance *inst = loader_get_instance(instance);
    VkIcdSurfaceDisplay *pIcdSurface = NULL;

    if (!inst->wsi_surface_enabled) {
        loader_log(inst, VK_DEBUG_REPORT_ERROR_BIT_EXT, 0,
                   "VK_KHR_surface extension not enabled.  "
                   "vkCreateDisplayPlaneSurfaceKHR not executed!\n");
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }

    pIcdSurface = loader_heap_alloc(inst, sizeof(VkIcdSurfaceDisplay),
                                    VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
    if (pIcdSurface == NULL) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    pIcdSurface->base.platform = VK_ICD_WSI_PLATFORM_DISPLAY;
    pIcdSurface->displayMode = pCreateInfo->displayMode;
    pIcdSurface->planeIndex = pCreateInfo->planeIndex;
    pIcdSurface->planeStackIndex = pCreateInfo->planeStackIndex;
    pIcdSurface->transform = pCreateInfo->transform;
    pIcdSurface->globalAlpha = pCreateInfo->globalAlpha;
    pIcdSurface->alphaMode = pCreateInfo->alphaMode;
    pIcdSurface->imageExtent = pCreateInfo->imageExtent;

    *pSurface = (VkSurfaceKHR)pIcdSurface;

    return VK_SUCCESS;
}

bool wsi_swapchain_instance_gpa(struct loader_instance *ptr_instance,
                                const char *name, void **addr) {
    *addr = NULL;

    /*
     * Functions for the VK_KHR_surface extension:
     */
    if (!strcmp("vkDestroySurfaceKHR", name)) {
        *addr = ptr_instance->wsi_surface_enabled ? (void *)vkDestroySurfaceKHR
                                                  : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceSurfaceSupportKHR", name)) {
        *addr = ptr_instance->wsi_surface_enabled
                    ? (void *)vkGetPhysicalDeviceSurfaceSupportKHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", name)) {
        *addr = ptr_instance->wsi_surface_enabled
                    ? (void *)vkGetPhysicalDeviceSurfaceCapabilitiesKHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceSurfaceFormatsKHR", name)) {
        *addr = ptr_instance->wsi_surface_enabled
                    ? (void *)vkGetPhysicalDeviceSurfaceFormatsKHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceSurfacePresentModesKHR", name)) {
        *addr = ptr_instance->wsi_surface_enabled
                    ? (void *)vkGetPhysicalDeviceSurfacePresentModesKHR
                    : NULL;
        return true;
    }

    /*
     * Functions for the VK_KHR_swapchain extension:
     *
     * Note: This is a device extension, and its functions are statically
     * exported from the loader.  Per Khronos decisions, the the loader's GIPA
     * function will return the trampoline function for such device-extension
     * functions, regardless of whether the extension has been enabled.
     */
    if (!strcmp("vkCreateSwapchainKHR", name)) {
        *addr = (void *)vkCreateSwapchainKHR;
        return true;
    }
    if (!strcmp("vkDestroySwapchainKHR", name)) {
        *addr = (void *)vkDestroySwapchainKHR;
        return true;
    }
    if (!strcmp("vkGetSwapchainImagesKHR", name)) {
        *addr = (void *)vkGetSwapchainImagesKHR;
        return true;
    }
    if (!strcmp("vkAcquireNextImageKHR", name)) {
        *addr = (void *)vkAcquireNextImageKHR;
        return true;
    }
    if (!strcmp("vkQueuePresentKHR", name)) {
        *addr = (void *)vkQueuePresentKHR;
        return true;
    }

#ifdef VK_USE_PLATFORM_WIN32_KHR
    /*
     * Functions for the VK_KHR_win32_surface extension:
     */
    if (!strcmp("vkCreateWin32SurfaceKHR", name)) {
        *addr = ptr_instance->wsi_win32_surface_enabled
                    ? (void *)vkCreateWin32SurfaceKHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceWin32PresentationSupportKHR", name)) {
        *addr = ptr_instance->wsi_win32_surface_enabled
                    ? (void *)vkGetPhysicalDeviceWin32PresentationSupportKHR
                    : NULL;
        return true;
    }
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
    /*
     * Functions for the VK_KHR_mir_surface extension:
     */
    if (!strcmp("vkCreateMirSurfaceKHR", name)) {
        *addr = ptr_instance->wsi_mir_surface_enabled
                    ? (void *)vkCreateMirSurfaceKHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceMirPresentationSupportKHR", name)) {
        *addr = ptr_instance->wsi_mir_surface_enabled
                    ? (void *)vkGetPhysicalDeviceMirPresentationSupportKHR
                    : NULL;
        return true;
    }
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    /*
     * Functions for the VK_KHR_wayland_surface extension:
     */
    if (!strcmp("vkCreateWaylandSurfaceKHR", name)) {
        *addr = ptr_instance->wsi_wayland_surface_enabled
                    ? (void *)vkCreateWaylandSurfaceKHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceWaylandPresentationSupportKHR", name)) {
        *addr = ptr_instance->wsi_wayland_surface_enabled
                    ? (void *)vkGetPhysicalDeviceWaylandPresentationSupportKHR
                    : NULL;
        return true;
    }
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    /*
     * Functions for the VK_KHR_xcb_surface extension:
     */
    if (!strcmp("vkCreateXcbSurfaceKHR", name)) {
        *addr = ptr_instance->wsi_xcb_surface_enabled
                    ? (void *)vkCreateXcbSurfaceKHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceXcbPresentationSupportKHR", name)) {
        *addr = ptr_instance->wsi_xcb_surface_enabled
                    ? (void *)vkGetPhysicalDeviceXcbPresentationSupportKHR
                    : NULL;
        return true;
    }
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    /*
     * Functions for the VK_KHR_xlib_surface extension:
     */
    if (!strcmp("vkCreateXlibSurfaceKHR", name)) {
        *addr = ptr_instance->wsi_xlib_surface_enabled
                    ? (void *)vkCreateXlibSurfaceKHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceXlibPresentationSupportKHR", name)) {
        *addr = ptr_instance->wsi_xlib_surface_enabled
                    ? (void *)vkGetPhysicalDeviceXlibPresentationSupportKHR
                    : NULL;
        return true;
    }
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
    /*
     * Functions for the VK_KHR_android_surface extension:
     */
    if (!strcmp("vkCreateAndroidSurfaceKHR", name)) {
        *addr = ptr_instance->wsi_xlib_surface_enabled
                    ? (void *)vkCreateAndroidSurfaceKHR
                    : NULL;
        return true;
    }
#endif // VK_USE_PLATFORM_ANDROID_KHR

    /*
     * Functions for VK_KHR_display extension:
     */
    if (!strcmp("vkGetPhysicalDeviceDisplayPropertiesKHR", name)) {
        *addr = ptr_instance->wsi_display_enabled
                    ? (void *)vkGetPhysicalDeviceDisplayPropertiesKHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetPhysicalDeviceDisplayPlanePropertiesKHR", name)) {
        *addr = ptr_instance->wsi_display_enabled
                    ? (void *)vkGetPhysicalDeviceDisplayPlanePropertiesKHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetDisplayPlaneSupportedDisplaysKHR", name)) {
        *addr = ptr_instance->wsi_display_enabled
                    ? (void *)vkGetDisplayPlaneSupportedDisplaysKHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetDisplayModePropertiesKHR", name)) {
        *addr = ptr_instance->wsi_display_enabled
                    ? (void *)vkGetDisplayModePropertiesKHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkCreateDisplayModeKHR", name)) {
        *addr = ptr_instance->wsi_display_enabled
                    ? (void *)vkCreateDisplayModeKHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkGetDisplayPlaneCapabilitiesKHR", name)) {
        *addr = ptr_instance->wsi_display_enabled
                    ? (void *)vkGetDisplayPlaneCapabilitiesKHR
                    : NULL;
        return true;
    }
    if (!strcmp("vkCreateDisplayPlaneSurfaceKHR", name)) {
        *addr = ptr_instance->wsi_display_enabled
                    ? (void *)vkCreateDisplayPlaneSurfaceKHR
                    : NULL;
        return true;
    }
    return false;
}
