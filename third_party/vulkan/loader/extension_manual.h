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

#pragma once

// ---- Manually added trampoline/terminator functions

// These functions, for whatever reason, require more complex changes than
// can easily be automatically generated.

VKAPI_ATTR VkResult VKAPI_CALL EnumeratePhysicalDeviceGroupsKHX(
    VkInstance instance, uint32_t *pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupPropertiesKHX *pPhysicalDeviceGroupProperties);

VKAPI_ATTR VkResult VKAPI_CALL terminator_EnumeratePhysicalDeviceGroupsKHX(
    VkInstance instance, uint32_t *pPhysicalDeviceGroupCount,
    VkPhysicalDeviceGroupPropertiesKHX *pPhysicalDeviceGroupProperties);

VKAPI_ATTR VkResult VKAPI_CALL
GetPhysicalDeviceExternalImageFormatPropertiesNV(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
    VkExternalMemoryHandleTypeFlagsNV externalHandleType,
    VkExternalImageFormatPropertiesNV *pExternalImageFormatProperties);

VKAPI_ATTR VkResult VKAPI_CALL
terminator_GetPhysicalDeviceExternalImageFormatPropertiesNV(
    VkPhysicalDevice physicalDevice, VkFormat format, VkImageType type,
    VkImageTiling tiling, VkImageUsageFlags usage, VkImageCreateFlags flags,
    VkExternalMemoryHandleTypeFlagsNV externalHandleType,
    VkExternalImageFormatPropertiesNV *pExternalImageFormatProperties);

