/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_BUFFER_RESOURCE_H_
#define XENIA_GPU_D3D11_D3D11_BUFFER_RESOURCE_H_

#include <xenia/gpu/buffer_resource.h>
#include <xenia/gpu/xenos/xenos.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {

class D3D11ResourceCache;


class D3D11IndexBufferResource : public IndexBufferResource {
public:
  D3D11IndexBufferResource(D3D11ResourceCache* resource_cache,
                           const MemoryRange& memory_range,
                           const Info& info);
  ~D3D11IndexBufferResource() override;

  void* handle() const override { return handle_; }

protected:
  int CreateHandle() override;
  int InvalidateRegion(const MemoryRange& memory_range) override;

private:
  D3D11ResourceCache* resource_cache_;
  ID3D11Buffer* handle_;
};


class D3D11VertexBufferResource : public VertexBufferResource {
public:
  D3D11VertexBufferResource(D3D11ResourceCache* resource_cache,
                            const MemoryRange& memory_range,
                            const Info& info);
  ~D3D11VertexBufferResource() override;

  void* handle() const override { return handle_; }

protected:
  int CreateHandle() override;
  int InvalidateRegion(const MemoryRange& memory_range) override;

private:
  D3D11ResourceCache* resource_cache_;
  ID3D11Buffer* handle_;
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_BUFFER_RESOURCE_H_
