/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gl4/gl4_gpu_flags.h"

DEFINE_bool(disable_framebuffer_readback, false,
            "Disable framebuffer readback.");
DEFINE_bool(disable_textures, false, "Disable textures and use colors only.");
