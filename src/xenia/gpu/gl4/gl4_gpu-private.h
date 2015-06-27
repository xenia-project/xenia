/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GL4_GL4_GPU_PRIVATE_H_
#define XENIA_GPU_GL4_GL4_GPU_PRIVATE_H_

#include <gflags/gflags.h>

DECLARE_bool(thread_safe_gl);
DECLARE_bool(disable_gl_context_reset);

DECLARE_bool(gl_debug);
DECLARE_bool(gl_debug_output);
DECLARE_bool(gl_debug_output_synchronous);

DECLARE_bool(vendor_gl_extensions);

DECLARE_bool(disable_framebuffer_readback);
DECLARE_bool(disable_textures);

#define FINE_GRAINED_DRAW_SCOPES 0

#endif  // XENIA_GPU_GL4_GL4_GPU_PRIVATE_H_
