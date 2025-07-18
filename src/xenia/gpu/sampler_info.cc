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

#include "xenia/base/xxhash.h"

namespace xe {
namespace gpu {

bool SamplerInfo::Prepare(const xenos::xe_gpu_texture_fetch_t& fetch,
                          const ParsedTextureFetchInstruction& fetch_instr,
                          SamplerInfo* out_info) {
  std::memset(out_info, 0, sizeof(SamplerInfo));

  out_info->min_filter =
      fetch_instr.attributes.min_filter == xenos::TextureFilter::kUseFetchConst
          ? fetch.min_filter
          : fetch_instr.attributes.min_filter;
  out_info->mag_filter =
      fetch_instr.attributes.mag_filter == xenos::TextureFilter::kUseFetchConst
          ? fetch.mag_filter
          : fetch_instr.attributes.mag_filter;
  out_info->mip_filter =
      fetch_instr.attributes.mip_filter == xenos::TextureFilter::kUseFetchConst
          ? fetch.mip_filter
          : fetch_instr.attributes.mip_filter;
  out_info->clamp_u = fetch.clamp_x;
  out_info->clamp_v = fetch.clamp_y;
  out_info->clamp_w = fetch.clamp_z;
  out_info->aniso_filter =
      fetch_instr.attributes.aniso_filter == xenos::AnisoFilter::kUseFetchConst
          ? fetch.aniso_filter
          : fetch_instr.attributes.aniso_filter;

  out_info->border_color = fetch.border_color;
  out_info->lod_bias = (fetch.lod_bias) / 32.f;
  out_info->mip_min_level = fetch.mip_min_level;
  out_info->mip_max_level = fetch.mip_max_level;

  return true;
}

uint64_t SamplerInfo::hash() const {
  return XXH3_64bits(this, sizeof(SamplerInfo));
}

}  //  namespace gpu
}  //  namespace xe
