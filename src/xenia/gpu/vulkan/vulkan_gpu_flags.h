/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_VULKAN_VULKAN_GPU_FLAGS_H_
#define XENIA_GPU_VULKAN_VULKAN_GPU_FLAGS_H_

#define FINE_GRAINED_DRAW_SCOPES 1
#include "xenia/base/cvar.h"

DECLARE_bool(vulkan_renderdoc_capture_all);
DECLARE_bool(vulkan_native_msaa);
DECLARE_bool(vulkan_dump_disasm);

#endif  // XENIA_GPU_VULKAN_VULKAN_GPU_FLAGS_H_
