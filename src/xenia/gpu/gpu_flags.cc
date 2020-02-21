/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gpu_flags.h"

DEFINE_string(trace_gpu_prefix, "scratch/gpu/",
              "Prefix path for GPU trace files.", "GPU");
DEFINE_bool(trace_gpu_stream, false, "Trace all GPU packets.", "GPU");

DEFINE_string(dump_shaders, "",
              "Path to write GPU shaders to as they are compiled.", "GPU");

DEFINE_bool(vsync, true, "Enable VSYNC.", "GPU");

DEFINE_bool(
    gpu_allow_invalid_fetch_constants, false,
    "Allow texture and vertex fetch constants with invalid type - generally "
    "unsafe because the constant may contain completely invalid values, but "
    "may be used to bypass fetch constant type errors in certain games until "
    "the real reason why they're invalid is found.",
    "GPU");
