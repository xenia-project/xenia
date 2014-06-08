/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_resource_cache.h>

#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/d3d11/d3d11_buffer_resource.h>
#include <xenia/gpu/d3d11/d3d11_sampler_state_resource.h>
#include <xenia/gpu/d3d11/d3d11_shader_resource.h>
#include <xenia/gpu/d3d11/d3d11_texture_resource.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;


D3D11ResourceCache::D3D11ResourceCache(Memory* memory,
                                       ID3D11Device* device,
                                       ID3D11DeviceContext* context)
    : ResourceCache(memory),
      device_(device), context_(context) {
  device_->AddRef();
  context_->AddRef();
}

D3D11ResourceCache::~D3D11ResourceCache() {
  XESAFERELEASE(device_);
  XESAFERELEASE(context_);
}

VertexShaderResource* D3D11ResourceCache::CreateVertexShader(
    const MemoryRange& memory_range,
    const VertexShaderResource::Info& info) {
  return new D3D11VertexShaderResource(this, memory_range, info);
}

PixelShaderResource* D3D11ResourceCache::CreatePixelShader(
    const MemoryRange& memory_range,
    const PixelShaderResource::Info& info) {
  return new D3D11PixelShaderResource(this, memory_range, info);
}

TextureResource* D3D11ResourceCache::CreateTexture(
    const MemoryRange& memory_range,
    const TextureResource::Info& info) {
  return new D3D11TextureResource(this, memory_range, info);
}

SamplerStateResource* D3D11ResourceCache::CreateSamplerState(
    const SamplerStateResource::Info& info) {
  return new D3D11SamplerStateResource(this, info);
}

IndexBufferResource* D3D11ResourceCache::CreateIndexBuffer(
    const MemoryRange& memory_range,
    const IndexBufferResource::Info& info) {
  return new D3D11IndexBufferResource(this, memory_range, info);
}

VertexBufferResource* D3D11ResourceCache::CreateVertexBuffer(
    const MemoryRange& memory_range,
    const VertexBufferResource::Info& info) {
  return new D3D11VertexBufferResource(this, memory_range, info);
}
