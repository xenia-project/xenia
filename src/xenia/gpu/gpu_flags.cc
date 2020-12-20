/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/gpu_flags.h"

DEFINE_path(trace_gpu_prefix, "scratch/gpu/",
            "Prefix path for GPU trace files.", "GPU");
DEFINE_bool(trace_gpu_stream, false, "Trace all GPU packets.", "GPU");

DEFINE_path(
    dump_shaders, "",
    "For shader debugging, path to dump GPU shaders to as they are compiled.",
    "GPU");

DEFINE_bool(vsync, true, "Enable VSYNC.", "GPU");

DEFINE_bool(
    gpu_allow_invalid_fetch_constants, false,
    "Allow texture and vertex fetch constants with invalid type - generally "
    "unsafe because the constant may contain completely invalid values, but "
    "may be used to bypass fetch constant type errors in certain games until "
    "the real reason why they're invalid is found.",
    "GPU");

DEFINE_bool(
    half_pixel_offset, true,
    "Enable support of vertex half-pixel offset (D3D9 PA_SU_VTX_CNTL "
    "PIX_CENTER). Generally games are aware of the half-pixel offset, and "
    "having this enabled is the correct behavior (disabling this may "
    "significantly break post-processing in some games, like Halo 3), but in "
    "some games it might have been ignored, resulting in slight blurriness of "
    "UI textures, for instance, when they are read between texels rather than "
    "at texel centers (Banjo-Kazooie), or the leftmost/topmost pixels may not "
    "be fully covered when MSAA is used with fullscreen passes.",
    "GPU");

DEFINE_bool(
    ssaa_scale_gradients, true,
    "When using SSAA instead of native MSAA, adjust texture coordinate "
    "derivatives used for mipmap selection, and getGradients results, to guest "
    "pixels as if true MSAA rather than SSAA was used.\n"
    "Reduces bandwidth usage of texture fetching.",
    "GPU");

DEFINE_string(
    depth_float24_conversion, "",
    "Method for converting 32-bit Z values to 20e4 floating point when using "
    "host depth buffers without native 20e4 support (when not using rasterizer-"
    "ordered views / fragment shader interlocks to perform depth testing "
    "manually).\n"
    "Use: [any, on_copy, truncate, round]\n"
    " on_copy:\n"
    "  Do depth testing at host precision, converting when copying between "
    "host depth buffers and the EDRAM buffer to support reinterpretation, "
    "maintaining two copies, in both host and 20e4 formats, for reloading data "
    "to host depth buffers when it wasn't overwritten.\n"
    "  + Highest performance, allows early depth test and writing.\n"
    "  + Host MSAA is possible with pixel-rate shading where supported.\n"
    "  - EDRAM > RAM > EDRAM depth buffer round trip done in certain games "
    "(such as GTA IV) destroys precision irreparably, causing artifacts if "
    "another rendering pass is done after the EDRAM reupload.\n"
    " truncate:\n"
    "  Convert to 20e4 directly in pixel shaders, always rounding down.\n"
    "  + Good performance, conservative early depth test is possible.\n"
    "  + No precision loss when anything changes in the storage of the depth "
    "buffer, EDRAM > RAM > EDRAM copying preserves precision.\n"
    "  - Rounding mode is incorrect, sometimes giving results smaller than "
    "they should be - may cause inaccuracy especially in edge cases when the "
    "game wants to write an exact value.\n"
    "  - Host MSAA is only possible at SSAA speed, with per-sample shading.\n"
    " round:\n"
    "  Convert to 20e4 directly in pixel shaders, correctly rounding to the "
    "nearest even.\n"
    "  + Highest accuracy.\n"
    "  - Significantly limited performance, early depth test is not possible.\n"
    "  - Host MSAA is only possible at SSAA speed, with per-sample shading.\n"
    " Any other value:\n"
    "  Choose what is considered the most optimal (currently \"on_copy\").",
    "GPU");

DEFINE_int32(query_occlusion_fake_sample_count, 1000,
             "If set to -1 no sample counts are written, games may hang. Else, "
             "the sample count of every tile will be incremented on every "
             "EVENT_WRITE_ZPD by this number. Setting this to 0 means "
             "everything is reported as occluded.",
             "GPU");

namespace xe {
namespace gpu {
namespace flags {

DepthFloat24Conversion GetDepthFloat24Conversion() {
  if (cvars::depth_float24_conversion == "truncate") {
    return DepthFloat24Conversion::kOnOutputTruncating;
  }
  if (cvars::depth_float24_conversion == "round") {
    return DepthFloat24Conversion::kOnOutputRounding;
  }
  return DepthFloat24Conversion::kOnCopy;
}

}  // namespace flags
}  // namespace gpu
}  // namespace xe
