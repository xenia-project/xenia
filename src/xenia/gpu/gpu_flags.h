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

DECLARE_bool(half_pixel_offset);

DECLARE_string(depth_float24_conversion);

DECLARE_int32(query_occlusion_fake_sample_count);

namespace xe {
namespace gpu {
namespace flags {

enum class DepthFloat24Conversion {
  // Doing depth test at the host precision, converting to 20e4 to support
  // reinterpretation, but keeping a separate EDRAM view containing depth values
  // in the host format. When copying from the EDRAM buffer to host depth
  // buffers, writing the stored host pixel if stored_f24 == to_f24(stored_host)
  // (otherwise it was overwritten by something else, like clearing, or a color
  // buffer; this is inexact though, and will incorrectly load pixels that were
  // overwritten by something else in the EDRAM, but turned out to have the same
  // value on the guest as before - an outdated host-precision value will be
  // loaded in these cases instead).
  //
  // EDRAM > RAM, then reusing the EDRAM region for something else > EDRAM round
  // trip destroys precision beyond repair.
  //
  // Full host early Z and MSAA with pixel-rate shading are supported.
  kOnCopy,
  // Converting the depth to the closest host value representable exactly as a
  // 20e4 float in pixel shaders, to support invariance in cases when the guest
  // reuploads a previously resolved depth buffer to the EDRAM, rounding towards
  // zero (which contradicts the rounding used by the Direct3D 9 reference
  // rasterizer, but allows less-than-or-equal pixel shader depth output to be
  // used to preserve most of early Z culling when the game is using reversed
  // depth, which is the usual way of doing depth testing on the Xbox 360 and of
  // utilizing the advantages of a floating-point encoding).
  //
  // With MSAA, pixel shaders must run at sample frequency - otherwise, if the
  // depth is the same for the entire pixel, intersections of polygons cannot be
  // antialiased.
  //
  // Important usage note: When using this mode, bounds of the fixed-function
  // viewport must be converted to and back from float24 too (preferably using
  // correct rounding to the nearest even, to reduce the error already caused by
  // truncation rather than to amplify it). This ensures that clamping to the
  // viewport bounds, which happens after the pixel shader even if it overwrites
  // the resulting depth, is never done to a value not representable as float24
  // (for example, if the minimum Z is a number too small to be represented as
  // float24, but not zero, it won't be possible to write what should become
  // 0x000000 to the depth buffer). Note that this may add some error to the
  // depth values from the rasterizer; however, modifying Z in the vertex shader
  // to make interpolated depth values would cause clipping to be done to
  // different bounds, which may be more undesirable, especially in cases when Z
  // is explicitly set to a value like 0 or W (in such cases, the adjusted
  // polygon may go outside 0...W in clip space and disappear).
  kOnOutputTruncating,
  // Similar to kOnOutputTruncating, but rounding to the nearest even, more
  // correctly, however, because the resulting depth can be bigger than the
  // original host value, early depth testing can't be used at all. Same
  // viewport usage rules apply.
  kOnOutputRounding,
};

DepthFloat24Conversion GetDepthFloat24Conversion();

}  // namespace flags
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_GPU_FLAGS_H_
