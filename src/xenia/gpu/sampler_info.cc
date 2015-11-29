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
                          const gl4::ucode::instr_fetch_tex_t& fetch_instr,
                          SamplerInfo* out_info) {
  std::memset(out_info, 0, sizeof(SamplerInfo));

  out_info->min_filter = static_cast<TextureFilter>(
      fetch_instr.min_filter == 3 ? fetch.min_filter : fetch_instr.min_filter);
  out_info->mag_filter = static_cast<TextureFilter>(
      fetch_instr.mag_filter == 3 ? fetch.mag_filter : fetch_instr.mag_filter);
  out_info->mip_filter = static_cast<TextureFilter>(
      fetch_instr.mip_filter == 3 ? fetch.mip_filter : fetch_instr.mip_filter);
  out_info->clamp_u = static_cast<ClampMode>(fetch.clamp_x);
  out_info->clamp_v = static_cast<ClampMode>(fetch.clamp_y);
  out_info->clamp_w = static_cast<ClampMode>(fetch.clamp_z);
  out_info->aniso_filter = static_cast<AnisoFilter>(
      fetch_instr.aniso_filter == 7 ? fetch.aniso_filter
                                    : fetch_instr.aniso_filter);

  return true;
}

uint64_t SamplerInfo::hash() const {
  return XXH64(this, sizeof(SamplerInfo), 0);
}

}  //  namespace gpu
}  //  namespace xe
