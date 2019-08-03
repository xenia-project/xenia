/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2016 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/vulkan/vulkan.h"

DEFINE_bool(vulkan_validation, false, "Enable Vulkan validation layers.",
            "Vulkan");

DEFINE_bool(vulkan_primary_queue_only, false,
            "Force the use of the primary queue, ignoring any additional that "
            "may be present.",
            "Vulkan");
