/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/d3d12/d3d12_api.h"

DEFINE_bool(d3d12_tiled_resources, true,
            "Enable tiled resources for shared memory emulation. Disabling "
            "them greatly increases video memory usage - a 512 MB buffer is "
            "created - but allows graphics debuggers that don't support tiled "
            "resources to work.");
