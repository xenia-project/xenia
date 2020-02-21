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

#include "xenia/base/platform.h"

#if XE_PLATFORM_WIN32
#define VK_USE_PLATFORM_WIN32_KHR 1
#elif XE_PLATFORM_LINUX
#define VK_USE_PLATFORM_XCB_KHR 1
#else
#error Platform not yet supported.
#endif  // XE_PLATFORM_WIN32

// We use a loader with its own function prototypes.
#include "third_party/volk/volk.h"
#include "third_party/vulkan/vulkan.h"
#include "xenia/base/cvar.h"

#define XELOGVK XELOGI

DECLARE_bool(vulkan_validation);
DECLARE_bool(vulkan_primary_queue_only);

#endif  // XENIA_UI_VULKAN_VULKAN_H_
