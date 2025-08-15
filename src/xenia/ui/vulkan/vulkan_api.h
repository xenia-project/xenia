/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_API_H_
#define XENIA_UI_VULKAN_VULKAN_API_H_

#include "xenia/base/assert.h"  // For Vulkan-Hpp.
#include "xenia/base/platform.h"

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#ifndef VK_ENABLE_BETA_EXTENSIONS
#define VK_ENABLE_BETA_EXTENSIONS
#endif

#if XE_PLATFORM_ANDROID
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#endif

#if XE_PLATFORM_GNU_LINUX
#ifndef VK_USE_PLATFORM_XCB_KHR
#define VK_USE_PLATFORM_XCB_KHR
#endif
#endif

#if XE_PLATFORM_WIN32
// Must be included before including vulkan.h with VK_USE_PLATFORM_WIN32_KHR
// because it includes Windows.h too.
#include "xenia/base/platform_win.h"
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#endif

#include "third_party/Vulkan-Headers/include/vulkan/vulkan.h"

#include "third_party/Vulkan-Headers/include/vulkan/vulkan_hpp_macros.hpp"
#include "third_party/Vulkan-Headers/include/vulkan/vulkan_to_string.hpp"

#endif  // XENIA_UI_VULKAN_VULKAN_API_H_
