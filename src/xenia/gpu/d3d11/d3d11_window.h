/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_D3D11_D3D11_WINDOW_H_
#define XENIA_GPU_D3D11_D3D11_WINDOW_H_

#include <xenia/core.h>

#include <xenia/ui/win32/win32_window.h>

#include <d3d11.h>


namespace xe {
namespace gpu {
namespace d3d11 {


class D3D11Window : public xe::ui::win32::Win32Window {
public:
  D3D11Window(
      xe_run_loop_ref run_loop,
      IDXGIFactory1* dxgi_factory, ID3D11Device* device);
  virtual ~D3D11Window();

  IDXGISwapChain* swap_chain() const { return swap_chain_; }

  virtual int Initialize(const char* title, uint32_t width, uint32_t height);

  void Swap();

protected:
  virtual bool OnResize(uint32_t width, uint32_t height);

private:
  IDXGIFactory1*          dxgi_factory_;
  ID3D11Device*           device_;
  IDXGISwapChain*         swap_chain_;
  ID3D11DeviceContext*    context_;
  ID3D11RenderTargetView* render_target_view_;
};


}  // namespace d3d11
}  // namespace gpu
}  // namespace xe


#endif  // XENIA_GPU_D3D11_D3D11_WINDOW_H_
