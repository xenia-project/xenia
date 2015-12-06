/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SAMPLER_INFO_H_
#define XENIA_GPU_SAMPLER_INFO_H_

#include "xenia/gpu/shader.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {

struct SamplerInfo {
  TextureFilter min_filter;
  TextureFilter mag_filter;
  TextureFilter mip_filter;
  ClampMode clamp_u;
  ClampMode clamp_v;
  ClampMode clamp_w;
  AnisoFilter aniso_filter;

  static bool Prepare(const xenos::xe_gpu_texture_fetch_t& fetch,
                      const ParsedTextureFetchInstruction& fetch_instr,
                      SamplerInfo* out_info);

  uint64_t hash() const;
  bool operator==(const SamplerInfo& other) const {
    return min_filter == other.min_filter && mag_filter == other.mag_filter &&
           mip_filter == other.mip_filter && clamp_u == other.clamp_u &&
           clamp_v == other.clamp_v && clamp_w == other.clamp_w &&
           aniso_filter == other.aniso_filter;
  }
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SAMPLER_INFO_H_
