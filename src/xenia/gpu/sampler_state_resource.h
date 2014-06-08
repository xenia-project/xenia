/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SAMPLER_STATE_RESOURCE_H_
#define XENIA_GPU_SAMPLER_STATE_RESOURCE_H_

#include <xenia/gpu/resource.h>
#include <xenia/gpu/xenos/ucode.h>
#include <xenia/gpu/xenos/xenos.h>


namespace xe {
namespace gpu {


class SamplerStateResource : public StaticResource {
public:
  struct Info {
    xenos::instr_tex_filter_t min_filter;
    xenos::instr_tex_filter_t mag_filter;
    xenos::instr_tex_filter_t mip_filter;
    uint32_t clamp_u;
    uint32_t clamp_v;
    uint32_t clamp_w;

    uint64_t hash() const {
      return hash_combine(0,
                          min_filter, mag_filter, mip_filter,
                          clamp_u, clamp_v, clamp_w);
    }
    bool Equals(const Info& other) const {
      return min_filter == other.min_filter &&
             mag_filter == other.mag_filter &&
             mip_filter == other.mip_filter &&
             clamp_u == other.clamp_u &&
             clamp_v == other.clamp_v &&
             clamp_w == other.clamp_w;
    }

    static bool Prepare(const xenos::xe_gpu_texture_fetch_t& fetch,
                        const xenos::instr_fetch_tex_t& fetch_instr,
                        Info& out_info);
  };

  SamplerStateResource(const Info& info) : info_(info) {}
  virtual ~SamplerStateResource() = default;

  const Info& info() const { return info_; }

  virtual int Prepare() = 0;

protected:
  Info info_;
};


}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_SAMPLER_STATE_RESOURCE_H_
