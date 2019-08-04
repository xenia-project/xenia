/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/vulkan/vulkan_gpu_flags.h"

DEFINE_bool(vulkan_renderdoc_capture_all, false,
            "Capture everything with RenderDoc.", "Vulkan");
DEFINE_bool(vulkan_native_msaa, false, "Use native MSAA", "Vulkan");
DEFINE_bool(vulkan_dump_disasm, false,
            "Dump shader disassembly. NVIDIA only supported.", "Vulkan");
