/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_GRAPHICS_DRIVER_H_
#define XENIA_GPU_D3D11_D3D11_GRAPHICS_DRIVER_H_

#include <xenia/core.h>

#include <xenia/gpu/graphics_driver.h>
#include <xenia/gpu/d3d11/d3d11-private.h>
#include <xenia/gpu/xenos/xenos.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {


class D3D11GraphicsDriver : public GraphicsDriver {
public:
  D3D11GraphicsDriver(xe_memory_ref memory, ID3D11Device* device);
  virtual ~D3D11GraphicsDriver();

  virtual void Initialize();

  virtual void InvalidateState(
      uint32_t mask);
  virtual void SetShader(
      xenos::XE_GPU_SHADER_TYPE type,
      uint32_t address,
      uint32_t start,
      uint32_t length);
  virtual void DrawIndexed(
      xenos::XE_GPU_PRIMITIVE_TYPE prim_type,
      uint32_t index_count);

private:
  ID3D11Device*   device_;
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_GRAPHICS_DRIVER_H_
