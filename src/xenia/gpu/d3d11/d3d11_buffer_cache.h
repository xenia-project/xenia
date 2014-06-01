/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_BUFFER_CACHE_H_
#define XENIA_GPU_D3D11_D3D11_BUFFER_CACHE_H_

#include <xenia/core.h>

#include <xenia/gpu/buffer_cache.h>
#include <xenia/gpu/xenos/xenos.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {


class D3D11BufferCache : public BufferCache {
public:
  D3D11BufferCache(ID3D11DeviceContext* context, ID3D11Device* device);
  virtual ~D3D11BufferCache();

  ID3D11DeviceContext* context() const { return context_; }
  ID3D11Device* device() const { return device_; }

protected:
  IndexBuffer* CreateIndexBuffer(
      const IndexBufferInfo& info,
      const uint8_t* src_ptr, size_t length) override;

protected:
  ID3D11DeviceContext* context_;
  ID3D11Device* device_;
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_BUFFER_CACHE_H_
