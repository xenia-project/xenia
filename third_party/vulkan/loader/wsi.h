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
 *
 */

#include "vk_loader_platform.h"
#include "loader.h"

bool wsi_swapchain_instance_gpa(struct loader_instance *ptr_instance,
                                const char *name, void **addr);

void wsi_create_instance(struct loader_instance *ptr_instance,
                         const VkInstanceCreateInfo *pCreateInfo);
bool wsi_unsupported_instance_extension(const VkExtensionProperties *ext_prop);

VKAPI_ATTR void VKAPI_CALL
terminator_DestroySurfaceKHR(VkInstance instance, VkSurfaceKHR surface,
                             const VkAllocationCallbacks *pAllocator);

VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice,
                                              uint32_t queueFamilyIndex,
                                              VkSurfaceKHR surface,
                                              VkBool32 *pSupported);

VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    VkSurfaceCapabilitiesKHR *pSurfaceCapabilities);

VKAPI_ATTR VkResult VKAPI_CALL terminator_GetPhysicalDeviceSurfaceFormatsKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t *pSurfaceFormatCount, VkSurfaceFormatKHR *pSurfaceFormats);

VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetPhysicalDeviceSurfacePresentModesKHR(
    VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
    uint32_t *pPresentModeCount, VkPresentModeKHR *pPresentModes);

#ifdef VK_USE_PLATFORM_WIN32_KHR
VKAPI_ATTR VkResult VKAPI_CALL
terminator_CreateWin32SurfaceKHR(VkInstance instance,
                                 const VkWin32SurfaceCreateInfoKHR *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator,
                                 VkSurfaceKHR *pSurface);
VKAPI_ATTR VkBool32 VKAPI_CALL
terminator_GetPhysicalDeviceWin32PresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex);
#endif
#ifdef VK_USE_PLATFORM_MIR_KHR
VKAPI_ATTR VkResult VKAPI_CALL
terminator_CreateMirSurfaceKHR(VkInstance instance,
                               const VkMirSurfaceCreateInfoKHR *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkSurfaceKHR *pSurface);
VKAPI_ATTR VkBool32 VKAPI_CALL
terminator_GetPhysicalDeviceMirPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    MirConnection *connection);
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
VKAPI_ATTR VkResult VKAPI_CALL terminator_CreateWaylandSurfaceKHR(
    VkInstance instance, const VkWaylandSurfaceCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface);
VKAPI_ATTR VkBool32 VKAPI_CALL
terminator_GetPhysicalDeviceWaylandPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    struct wl_display *display);
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
VKAPI_ATTR VkResult VKAPI_CALL
terminator_CreateXcbSurfaceKHR(VkInstance instance,
                               const VkXcbSurfaceCreateInfoKHR *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkSurfaceKHR *pSurface);

VKAPI_ATTR VkBool32 VKAPI_CALL
terminator_GetPhysicalDeviceXcbPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex,
    xcb_connection_t *connection, xcb_visualid_t visual_id);
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
VKAPI_ATTR VkResult VKAPI_CALL
terminator_CreateXlibSurfaceKHR(VkInstance instance,
                                const VkXlibSurfaceCreateInfoKHR *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkSurfaceKHR *pSurface);
VKAPI_ATTR VkBool32 VKAPI_CALL
terminator_GetPhysicalDeviceXlibPresentationSupportKHR(
    VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, Display *dpy,
    VisualID visualID);
#endif
VKAPI_ATTR VkResult VKAPI_CALL terminator_GetPhysicalDeviceDisplayPropertiesKHR(
    VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
    VkDisplayPropertiesKHR *pProperties);
VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetPhysicalDeviceDisplayPlanePropertiesKHR(
    VkPhysicalDevice physicalDevice, uint32_t *pPropertyCount,
    VkDisplayPlanePropertiesKHR *pProperties);
VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetDisplayPlaneSupportedDisplaysKHR(VkPhysicalDevice physicalDevice,
                                               uint32_t planeIndex,
                                               uint32_t *pDisplayCount,
                                               VkDisplayKHR *pDisplays);
VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetDisplayModePropertiesKHR(VkPhysicalDevice physicalDevice,
                                       VkDisplayKHR display,
                                       uint32_t *pPropertyCount,
                                       VkDisplayModePropertiesKHR *pProperties);
VKAPI_ATTR VkResult VKAPI_CALL
terminator_CreateDisplayModeKHR(VkPhysicalDevice physicalDevice,
                                VkDisplayKHR display,
                                const VkDisplayModeCreateInfoKHR *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkDisplayModeKHR *pMode);
VKAPI_ATTR VkResult VKAPI_CALL terminator_GetDisplayPlaneCapabilitiesKHR(
    VkPhysicalDevice physicalDevice, VkDisplayModeKHR mode, uint32_t planeIndex,
    VkDisplayPlaneCapabilitiesKHR *pCapabilities);
VKAPI_ATTR VkResult VKAPI_CALL terminator_CreateDisplayPlaneSurfaceKHR(
    VkInstance instance, const VkDisplaySurfaceCreateInfoKHR *pCreateInfo,
    const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface);
