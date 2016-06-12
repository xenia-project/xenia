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
 *
 */

#include "vk_loader_platform.h"
#include "loader.h"

bool wsi_swapchain_instance_gpa(struct loader_instance *ptr_instance,
                                const char *name, void **addr);
void wsi_add_instance_extensions(const struct loader_instance *inst,
                                 struct loader_extension_list *ext_list);

void wsi_create_instance(struct loader_instance *ptr_instance,
                         const VkInstanceCreateInfo *pCreateInfo);

VKAPI_ATTR void VKAPI_CALL
loader_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                         const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL
loader_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice,
                                          uint32_t queueFamilyIndex,
                                          VkSurfaceKHR surface,
                                          VkBool32 *pSupported);

VKAPI_ATTR VkResult VKAPI_CALL loader_GetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR *pSurfaceCapabilities);

VKAPI_ATTR VkResult VKAPI_CALL
loader_GetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice physicalDevice,
                                          VkSurfaceKHR surface,
                                          uint32_t *pSurfaceFormatCount,
                                          VkSurfaceFormatKHR *pSurfaceFormats);

VKAPI_ATTR VkResult VKAPI_CALL
loader_GetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice physicalDevice,
                                               VkSurfaceKHR surface,
                                               uint32_t *pPresentModeCount,
                                               VkPresentModeKHR *pPresentModes);

#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL
loader_CreateWin32SurfaceKHR(VkInstance instance,
                             const VkWin32SurfaceCreateInfoKHR *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator,
                             VkSurfaceKHR *pSurface);
VKAPI_ATTR VkBool32 VKAPI_CALL
loader_GetPhysicalDeviceWin32PresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex);
#endif
#ifdef VK_USE_PLATFORM_MIR_KHR
VKAPI_ATTR VkResult VKAPI_CALL
loader_CreateMirSurfaceKHR(VkInstance instance,
                           const VkMirSurfaceCreateInfoKHR *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkSurfaceKHR *pSurface);
VKAPI_ATTR VkBool32 VKAPI_CALL
loader_GetPhysicalDeviceMirPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    MirConnection *connection);
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
VKAPI_ATTR VkResult VKAPI_CALL
loader_CreateWaylandSurfaceKHR(VkInstance instance,
                               const VkWaylandSurfaceCreateInfoKHR *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkSurfaceKHR *pSurface);
VKAPI_ATTR VkBool32 VKAPI_CALL
loader_GetPhysicalDeviceWaylandPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    struct wl_display *display);
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
VKAPI_ATTR VkResult VKAPI_CALL
loader_CreateXcbSurfaceKHR(VkInstance instance,
                           const VkXcbSurfaceCreateInfoKHR *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator,
                           VkSurfaceKHR *pSurface);

VKAPI_ATTR VkBool32 VKAPI_CALL
loader_GetPhysicalDeviceXcbPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    xcb_connection_t *connection, xcb_visualid_t visual_id);
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
VKAPI_ATTR VkResult VKAPI_CALL
loader_CreateXlibSurfaceKHR(VkInstance instance,
                            const VkXlibSurfaceCreateInfoKHR *pCreateInfo,
                            const VkAllocationCallbacks *pAllocator,
                            VkSurfaceKHR *pSurface);
VKAPI_ATTR VkBool32 VKAPI_CALL
loader_GetPhysicalDeviceXlibPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, Display *dpy,
    VisualID visualID);
#endif
