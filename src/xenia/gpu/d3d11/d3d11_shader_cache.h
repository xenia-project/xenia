/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_SHADER_CACHE_H_
#define XENIA_GPU_D3D11_D3D11_SHADER_CACHE_H_

#include <xenia/core.h>

#include <xenia/gpu/shader_cache.h>

#include <D3D11.h>


namespace xe {
namespace gpu {
namespace d3d11 {


class D3D11ShaderCache : public ShaderCache {
public:
  D3D11ShaderCache(ID3D11Device* device);
  virtual ~D3D11ShaderCache();

protected:
  Shader* CreateCore(
      xenos::XE_GPU_SHADER_TYPE type,
      const uint8_t* src_ptr, size_t length,
      uint64_t hash) override;

protected:
  ID3D11Device* device_;
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_SHADER_CACHE_H_
