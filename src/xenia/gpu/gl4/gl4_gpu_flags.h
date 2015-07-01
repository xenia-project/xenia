/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GL4_GL4_GPU_FLAGS_H_
#define XENIA_GPU_GL4_GL4_GPU_FLAGS_H_

#include <gflags/gflags.h>

DECLARE_bool(disable_framebuffer_readback);
DECLARE_bool(disable_textures);

#define FINE_GRAINED_DRAW_SCOPES 0

#endif  // XENIA_GPU_GL4_GL4_GPU_FLAGS_H_
