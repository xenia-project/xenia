/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VULKAN_VULKAN_H_
#define XENIA_UI_VULKAN_VULKAN_H_

#include "xenia/base/cvar.h"
#include "xenia/base/platform.h"

#if XE_PLATFORM_ANDROID
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR 1
#endif
#elif XE_PLATFORM_GNU_LINUX
#ifndef VK_USE_PLATFORM_XCB_KHR
#define VK_USE_PLATFORM_XCB_KHR 1
#endif
#elif XE_PLATFORM_WIN32
// Must be included before vulkan.h with VK_USE_PLATFORM_WIN32_KHR because it
// includes Windows.h too.
#include "xenia/base/platform_win.h"
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR 1
#endif
#endif

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES 1
#endif
#include "third_party/vulkan/vulkan.h"

#define XELOGVK XELOGI

DECLARE_bool(vulkan_validation);
DECLARE_bool(vulkan_primary_queue_only);

#endif  // XENIA_UI_VULKAN_VULKAN_H_
