/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_SAMPLER_STATE_RESOURCE_H_
#define XENIA_GPU_D3D11_D3D11_SAMPLER_STATE_RESOURCE_H_

#include <xenia/gpu/sampler_state_resource.h>
#include <xenia/gpu/xenos/ucode.h>
#include <xenia/gpu/xenos/xenos.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {

class D3D11ResourceCache;


class D3D11SamplerStateResource : public SamplerStateResource {
public:
  D3D11SamplerStateResource(D3D11ResourceCache* resource_cache,
                            const Info& info);
  ~D3D11SamplerStateResource() override;

  void* handle() const override { return handle_; }

  int Prepare() override;

protected:
  D3D11ResourceCache* resource_cache_;
  ID3D11SamplerState* handle_;
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_SAMPLER_STATE_RESOURCE_H_
