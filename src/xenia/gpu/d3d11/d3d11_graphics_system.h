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
#include <xenia/gpu/d3d11/d3d11_gpu-private.h>


namespace xe {
namespace gpu {
namespace d3d11 {

class D3D11Window;


std::unique_ptr<GraphicsSystem> Create(Emulator* emulator);


class D3D11GraphicsSystem : public GraphicsSystem {
public:
  D3D11GraphicsSystem(Emulator* emulator);
  virtual ~D3D11GraphicsSystem();

  virtual void Shutdown();

  void Swap() override;

protected:
  virtual void Initialize();
  virtual void Pump();

private:
  static void __stdcall VsyncCallback(D3D11GraphicsSystem* gs, BOOLEAN);

  IDXGIFactory1*  dxgi_factory_;
  ID3D11Device*   device_;
  D3D11Window*    window_;

  HANDLE          timer_queue_;
  HANDLE          vsync_timer_;

  std::chrono::high_resolution_clock::time_point last_swap_time_;
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_GRAPHICS_SYSTEM_H_
