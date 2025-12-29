/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_GPU_FLAGS_H_
#define XENIA_GPU_GPU_FLAGS_H_
#include "xenia/base/cvar.h"

DECLARE_path(trace_gpu_prefix);
DECLARE_bool(trace_gpu_stream);

DECLARE_path(dump_shaders);

DECLARE_bool(vsync);

DECLARE_bool(gpu_allow_invalid_fetch_constants);

DECLARE_bool(non_seamless_cube_map);
DECLARE_bool(half_pixel_offset);

// Metal-specific debug flag: make EDRAM buffer CPU-visible so its contents
// can be inspected from resolve code. Only use in debug builds.
DECLARE_bool(metal_debug_edram_cpu_visible);
// Metal-specific: enable ROV-style EDRAM path when available.
DECLARE_bool(metal_edram_rov);

DECLARE_int32(query_occlusion_fake_sample_count);

#endif  // XENIA_GPU_GPU_FLAGS_H_
