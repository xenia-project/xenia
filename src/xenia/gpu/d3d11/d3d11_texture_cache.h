/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_TEXTURE_CACHE_H_
#define XENIA_GPU_D3D11_D3D11_TEXTURE_CACHE_H_

#include <xenia/core.h>

#include <xenia/gpu/texture_cache.h>
#include <xenia/gpu/shader.h>
#include <xenia/gpu/d3d11/d3d11_texture.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {


class D3D11TextureCache : public TextureCache {
public:
  D3D11TextureCache(Memory* memory,
                    ID3D11DeviceContext* context, ID3D11Device* device);
  virtual ~D3D11TextureCache();

  ID3D11DeviceContext* context() const { return context_; }
  ID3D11Device* device() const { return device_; }

  ID3D11SamplerState* GetSamplerState(
      const xenos::xe_gpu_texture_fetch_t& fetch,
      const Shader::tex_buffer_desc_t& desc);

protected:
  Texture* CreateTexture(uint32_t address, const uint8_t* host_address,
                         const xenos::xe_gpu_texture_fetch_t& fetch) override;

private:
  ID3D11DeviceContext* context_;
  ID3D11Device* device_;

  struct CachedSamplerState {
    D3D11_SAMPLER_DESC desc;
    ID3D11SamplerState* state;
  };
  std::unordered_multimap<size_t, CachedSamplerState> samplers_;
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_TEXTURE_CACHE_H_
