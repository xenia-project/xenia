/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/sampler_info.h"

#include <cstring>
#include <memory>

#include "third_party/xxhash/xxhash.h"

namespace xe {
namespace gpu {

bool SamplerInfo::Prepare(const xenos::xe_gpu_texture_fetch_t& fetch,
                          const ParsedTextureFetchInstruction& fetch_instr,
                          SamplerInfo* out_info) {
  std::memset(out_info, 0, sizeof(SamplerInfo));

  out_info->min_filter =
      fetch_instr.attributes.min_filter == TextureFilter::kUseFetchConst
          ? static_cast<TextureFilter>(fetch.min_filter)
          : fetch_instr.attributes.min_filter;
  out_info->mag_filter =
      fetch_instr.attributes.mag_filter == TextureFilter::kUseFetchConst
          ? static_cast<TextureFilter>(fetch.mag_filter)
          : fetch_instr.attributes.mag_filter;
  out_info->mip_filter =
      fetch_instr.attributes.mip_filter == TextureFilter::kUseFetchConst
          ? static_cast<TextureFilter>(fetch.mip_filter)
          : fetch_instr.attributes.mip_filter;
  out_info->clamp_u = static_cast<ClampMode>(fetch.clamp_x);
  out_info->clamp_v = static_cast<ClampMode>(fetch.clamp_y);
  out_info->clamp_w = static_cast<ClampMode>(fetch.clamp_z);
  out_info->aniso_filter =
      fetch_instr.attributes.aniso_filter == AnisoFilter::kUseFetchConst
          ? static_cast<AnisoFilter>(fetch.aniso_filter)
          : fetch_instr.attributes.aniso_filter;

  return true;
}

uint64_t SamplerInfo::hash() const {
  return XXH64(this, sizeof(SamplerInfo), 0);
}

}  //  namespace gpu
}  //  namespace xe
