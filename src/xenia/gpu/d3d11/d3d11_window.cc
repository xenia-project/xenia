/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_window.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;
using namespace xe::ui;
using namespace xe::ui::win32;


D3D11Window::D3D11Window(
    xe_run_loop_ref run_loop,
    IDXGIFactory1* dxgi_factory, ID3D11Device* device) :
    Win32Window(run_loop) {
  dxgi_factory_ = dxgi_factory;
  dxgi_factory_->AddRef();
  device_ = device;
  device_->AddRef();
  device_->GetImmediateContext(&context_);

  swap_chain_ = 0;
  render_target_view_ = 0;

  closing.AddListener([](UIEvent& e) {
    xe_run_loop_quit(e.window()->run_loop());
  });
}

D3D11Window::~D3D11Window() {
  if (context_) {
    context_->ClearState();
  }
  XESAFERELEASE(render_target_view_);
  XESAFERELEASE(context_);
  XESAFERELEASE(swap_chain_);
  XESAFERELEASE(device_);
  XESAFERELEASE(dxgi_factory_);
}

int D3D11Window::Initialize(const char* title, uint32_t width, uint32_t height) {
  int result = Win32Window::Initialize(title, width, height);
  if (result) {
    return result;
  }

  // Setup swap chain.
  DXGI_SWAP_CHAIN_DESC desc;
  xe_zero_struct(&desc, sizeof(desc));
  desc.OutputWindow       = handle();
  desc.Windowed           = TRUE;
  desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  desc.Flags              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

  // Setup buffers.
  desc.BufferCount        = 2;
  desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferDesc.Width   = width;
  desc.BufferDesc.Height  = height;
  desc.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.BufferDesc.RefreshRate.Numerator   = 60;
  desc.BufferDesc.RefreshRate.Denominator = 1;

  // Disable multisampling.
  desc.SampleDesc.Count   = 1;
  desc.SampleDesc.Quality = 0;

  // Create!
  HRESULT hr = dxgi_factory_->CreateSwapChain(
      device_,
      &desc,
      &swap_chain_);
  if (FAILED(hr)) {
    XELOGE("CreateSwapChain failed with %.8X", hr);
    return 1;
  }

  // Create a render target view to draw into.
  ID3D11Texture2D* back_buffer = 0;
  hr = swap_chain_->GetBuffer(
      0, __uuidof(ID3D11Texture2D), (void**)&back_buffer);
  if (FAILED(hr)) {
    XELOGE("GetBuffer (back_buffer) failed with %.8X", hr);
    return 1;
  }
  hr = device_->CreateRenderTargetView(
      back_buffer, NULL, &render_target_view_);
  back_buffer->Release();
  if (FAILED(hr)) {
    XELOGE("CreateRenderTargetView (back_buffer) failed with %.8X", hr);
    return 1;
  }
  context_->OMSetRenderTargets(1, &render_target_view_, NULL);

  return 0;
}

void D3D11Window::Swap() {
  // Swap buffers.
  // TODO(benvanik): control vsync with flag.
  bool vsync = true;
  HRESULT hr = swap_chain_->Present(vsync ? 1 : 0, 0);
  if (FAILED(hr)) {
    XELOGE("Present failed with %.8X", hr);
  }
}

bool D3D11Window::OnResize(uint32_t width, uint32_t height) {
  if (!Win32Window::OnResize(width, height)) {
    return false;
  }

  // TODO(benvanik): resize swap buffers?

  return true;
}

