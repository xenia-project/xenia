/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_GRAPHICS_SYSTEM_H_
#define XENIA_GPU_D3D11_D3D11_GRAPHICS_SYSTEM_H_

#include <xenia/core.h>

#include <xenia/gpu/graphics_system.h>
#include <xenia/gpu/d3d11/d3d11-private.h>


namespace xe {
namespace gpu {
namespace d3d11 {


GraphicsSystem* Create(const CreationParams* params);


class D3D11GraphicsSystem : public GraphicsSystem {
public:
  D3D11GraphicsSystem(const CreationParams* params);
  virtual ~D3D11GraphicsSystem();
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_GRAPHICS_SYSTEM_H_
