/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_RESOURCE_CACHE_H_
#define XENIA_GPU_D3D11_D3D11_RESOURCE_CACHE_H_

#include <xenia/core.h>

#include <xenia/gpu/resource_cache.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {


class D3D11ResourceCache : public ResourceCache {
public:
  D3D11ResourceCache(Memory* memory,
                     ID3D11Device* device, ID3D11DeviceContext* context);
  virtual ~D3D11ResourceCache();
  
  ID3D11Device* device() const { return device_; }
  ID3D11DeviceContext* context() const { return context_; }

protected:
  VertexShaderResource* CreateVertexShader(
      const MemoryRange& memory_range,
      const VertexShaderResource::Info& info) override;
  PixelShaderResource* CreatePixelShader(
      const MemoryRange& memory_range,
      const PixelShaderResource::Info& info) override;
  TextureResource* CreateTexture(
      const MemoryRange& memory_range,
      const TextureResource::Info& info) override;
  SamplerStateResource* CreateSamplerState(
      const SamplerStateResource::Info& info) override;
  IndexBufferResource* CreateIndexBuffer(
      const MemoryRange& memory_range,
      const IndexBufferResource::Info& info) override;
  VertexBufferResource* CreateVertexBuffer(
      const MemoryRange& memory_range,
      const VertexBufferResource::Info& info) override;

private:
  ID3D11Device* device_;
  ID3D11DeviceContext* context_;
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_RESOURCE_CACHE_H_
