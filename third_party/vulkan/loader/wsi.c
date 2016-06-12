/*
 * Copyright (c) 2015-2016 The Khronos Group Inc.
 * Copyright (c) 2015-2016 Valve Corporation
 * Copyright (c) 2015-2016 LunarG, Inc.
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
 * Author: Ian Elliott <ian@lunarg.com>
 * Author: Jon Ashburn <jon@lunarg.com>
 * Author: Ian Elliott <ianelliott@google.com>
 * Author: Mark Lobodzinski <mark@lunarg.com>
 */

//#define _ISOC11_SOURCE /* for aligned_alloc() */
#define _GNU_SOURCE
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

void wsi_add_instance_extensions(const struct loader_instance *inst,
                                 struct loader_extension_list *ext_list) {
    loader_add_to_ext_list(inst, ext_list, 1, &wsi_surface_extension_info);
#ifdef VK_USE_PLATFORM_WIN32_KHR
    loader_add_to_ext_list(inst, ext_list, 1,
                           &wsi_win32_surface_extension_info);
#endif // VK_USE_PLATFORM_WIN32_KHR
#ifdef VK_USE_PLATFORM_MIR_KHR
    loader_add_to_ext_list(inst, ext_list, 1, &wsi_mir_surface_extension_info);
#endif // VK_USE_PLATFORM_MIR_KHR
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
    loader_add_to_ext_list(inst, ext_list, 1,
                           &wsi_wayland_surface_extension_info);
#endif // VK_USE_PLATFORM_WAYLAND_KHR
#ifdef VK_USE_PLATFORM_XCB_KHR
    loader_add_to_ext_list(inst, ext_list, 1, &wsi_xcb_surface_extension_info);
#endif // VK_USE_PLATFORM_XCB_KHR
#ifdef VK_USE_PLATFORM_XLIB_KHR
    loader_add_to_ext_list(inst, ext_list, 1, &wsi_xlib_surface_extension_info);
#endif // VK_USE_PLATFORM_XLIB_KHR
#ifdef VK_USE_PLATFORM_ANDROID_KHR
    loader_add_to_ext_list(inst, ext_list, 1,
                           &wsi_android_surface_extension_info);
#endif // VK_USE_PLATFORM_ANDROID_KHR
}

void wsi_create_instance(struct loader_instance *ptr_instance,
                         const VkInstanceCreateInfo *pCreateInfo) {
    ptr_instance->wsi_surface_enabled = false;

#ifdef VK_USE_PLATFORM_WIN32_KHR
    ptr_instance->wsi_win32_surface_enabled = true;
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
    }
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

/*
 * This is the instance chain terminator function
 * for DestroySurfaceKHR
 */
VKAPI_ATTR void VKAPI_CALL
loader_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
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
    disp = loader_get_instance_dispatch(physicalDevice);
    VkResult res = disp->GetPhysicalDeviceSurfaceSupportKHR(
        physicalDevice, queueFamilyIndex, surface, pSupported);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceSurfaceSupportKHR
 */
VKAPI_ATTR VkResult VKAPI_CALL
loader_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice,
                                          uint32_t queueFamilyIndex,
                                          VkSurfaceKHR surface,
                                          VkBool32 *pSupported) {
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
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
    disp = loader_get_instance_dispatch(physicalDevice);
    VkResult res = disp->GetPhysicalDeviceSurfaceCapabilitiesKHR(
        physicalDevice, surface, pSurfaceCapabilities);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceSurfaceCapabilitiesKHR
 */
VKAPI_ATTR VkResult VKAPI_CALL loader_GetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR *pSurfaceCapabilities) {
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
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
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkResult res = disp->GetPhysicalDeviceSurfaceFormatsKHR(
        physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceSurfaceFormatsKHR
 */
VKAPI_ATTR VkResult VKAPI_CALL
loader_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice,
                                          VkSurfaceKHR surface,
                                          uint32_t *pSurfaceFormatCount,
                                          VkSurfaceFormatKHR *pSurfaceFormats) {
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
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
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkResult res = disp->GetPhysicalDeviceSurfacePresentModesKHR(
        physicalDevice, surface, pPresentModeCount, pPresentModes);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceSurfacePresentModesKHR
 */
VKAPI_ATTR VkResult VKAPI_CALL loader_GetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes) {
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
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
loader_CreateWin32SurfaceKHR(VkInstance instance,
                             const VkWin32SurfaceCreateInfoKHR *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator,
                             VkSurfaceKHR *pSurface) {
    struct loader_instance *ptr_instance = loader_get_instance(instance);
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
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkBool32 res = disp->GetPhysicalDeviceWin32PresentationSupportKHR(
        physicalDevice, queueFamilyIndex);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceWin32PresentationSupportKHR
 */
VKAPI_ATTR VkBool32 VKAPI_CALL
loader_GetPhysicalDeviceWin32PresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex) {
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
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
loader_CreateMirSurfaceKHR(VkInstance instance,
                           const VkMirSurfaceCreateInfoKHR *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkSurfaceKHR *pSurface) {
    struct loader_instance *ptr_instance = loader_get_instance(instance);
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
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkBool32 res = disp->GetPhysicalDeviceMirPresentationSupportKHR(
        physicalDevice, queueFamilyIndex, connection);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceMirPresentationSupportKHR
 */
VKAPI_ATTR VkBool32 VKAPI_CALL
loader_GetPhysicalDeviceMirPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    MirConnection *connection) {
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
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
                          const VkMirSurfaceCreateInfoKHR *pCreateInfo,
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
 * for CreateXlibSurfaceKHR
 */
VKAPI_ATTR VkResult VKAPI_CALL
loader_CreateWaylandSurfaceKHR(VkInstance instance,
                               const VkMirSurfaceCreateInfoKHR *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkSurfaceKHR *pSurface) {
    struct loader_instance *ptr_instance = loader_get_instance(instance);
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
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkBool32 res = disp->GetPhysicalDeviceWaylandPresentationSupportKHR(
        physicalDevice, queueFamilyIndex, display);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceWaylandPresentationSupportKHR
 */
VKAPI_ATTR VkBool32 VKAPI_CALL
loader_GetPhysicalDeviceWaylandPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    struct wl_display *display) {
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
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
loader_CreateXcbSurfaceKHR(VkInstance instance,
                           const VkXcbSurfaceCreateInfoKHR *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkSurfaceKHR *pSurface) {
    struct loader_instance *ptr_instance = loader_get_instance(instance);
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
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkBool32 res = disp->GetPhysicalDeviceXcbPresentationSupportKHR(
        physicalDevice, queueFamilyIndex, connection, visual_id);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceXcbPresentationSupportKHR
 */
VKAPI_ATTR VkBool32 VKAPI_CALL
loader_GetPhysicalDeviceXcbPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    xcb_connection_t *connection, xcb_visualid_t visual_id) {
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
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
loader_CreateXlibSurfaceKHR(VkInstance instance,
                            const VkXlibSurfaceCreateInfoKHR *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkSurfaceKHR *pSurface) {
    struct loader_instance *ptr_instance = loader_get_instance(instance);
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
    const VkLayerInstanceDispatchTable *disp;
    disp = loader_get_instance_dispatch(physicalDevice);
    VkBool32 res = disp->GetPhysicalDeviceXlibPresentationSupportKHR(
        physicalDevice, queueFamilyIndex, dpy, visualID);
    return res;
}

/*
 * This is the instance chain terminator function
 * for GetPhysicalDeviceXlibPresentationSupportKHR
 */
VKAPI_ATTR VkBool32 VKAPI_CALL
loader_GetPhysicalDeviceXlibPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, Display *dpy,
    VisualID visualID) {
    struct loader_physical_device *phys_dev =
        (struct loader_physical_device *)physicalDevice;
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
loader_CreateAndroidSurfaceKHR(VkInstance instance, Window window,
                               const VkAllocationCallbacks *pAllocator,
                               VkSurfaceKHR *pSurface) {
    struct loader_instance *ptr_instance = loader_get_instance(instance);
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
            *addr =
                ptr_instance->wsi_wayland_surface_enabled
                    ? (void *)vkGetPhysicalDeviceWaylandPresentationSupportKHR
                    : NULL;
            return true;
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
                *addr =
                    ptr_instance->wsi_xcb_surface_enabled
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
            if (!strcmp("vkGetPhysicalDeviceXlibPresentationSupportKHR",
                        name)) {
                *addr =
                    ptr_instance->wsi_xlib_surface_enabled
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

            return false;
        }
