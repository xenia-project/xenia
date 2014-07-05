/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/sampler_state_resource.h>


using namespace std;
using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::xenos;


bool SamplerStateResource::Info::Prepare(
    const xe_gpu_texture_fetch_t& fetch, const instr_fetch_tex_t& fetch_instr,
    Info& out_info) {
  out_info.min_filter = static_cast<instr_tex_filter_t>(
      fetch_instr.min_filter == 3 ? fetch.min_filter : fetch_instr.min_filter);
  out_info.mag_filter = static_cast<instr_tex_filter_t>(
      fetch_instr.mag_filter == 3 ? fetch.mag_filter : fetch_instr.mag_filter);
  out_info.mip_filter = static_cast<instr_tex_filter_t>(
      fetch_instr.mip_filter == 3 ? fetch.mip_filter : fetch_instr.mip_filter);
  out_info.clamp_u = fetch.clamp_x;
  out_info.clamp_v = fetch.clamp_y;
  out_info.clamp_w = fetch.clamp_z;
  return true;
}
