/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_TEXTURE_RESOURCE_H_
#define XENIA_GPU_D3D11_D3D11_TEXTURE_RESOURCE_H_

#include <xenia/gpu/texture_resource.h>
#include <xenia/gpu/xenos/xenos.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {

class D3D11ResourceCache;


class D3D11TextureResource : public TextureResource {
public:
  D3D11TextureResource(D3D11ResourceCache* resource_cache,
                       const MemoryRange& memory_range,
                       const Info& info);
  ~D3D11TextureResource() override;

  void* handle() const override { return handle_; }

protected:
  int CreateHandle() override;
  int CreateHandle1D();
  int CreateHandle2D();
  int CreateHandle3D();
  int CreateHandleCube();

  int InvalidateRegion(const MemoryRange& memory_range) override;
  int InvalidateRegion1D(const MemoryRange& memory_range);
  int InvalidateRegion2D(const MemoryRange& memory_range);
  int InvalidateRegion3D(const MemoryRange& memory_range);
  int InvalidateRegionCube(const MemoryRange& memory_range);

private:
  D3D11ResourceCache* resource_cache_;
  ID3D11Resource* texture_;
  ID3D11ShaderResourceView* handle_;
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_TEXTURE_RESOURCE_H_
