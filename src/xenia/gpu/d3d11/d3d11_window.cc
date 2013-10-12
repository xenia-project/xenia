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
using namespace xe::core;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;


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

  // Setup swap chain.
  DXGI_SWAP_CHAIN_DESC desc;
  xe_zero_struct(&desc, sizeof(desc));
  desc.OutputWindow       = handle_;
  desc.Windowed           = TRUE;
  desc.SwapEffect         = DXGI_SWAP_EFFECT_DISCARD;
  desc.Flags              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

  // Setup buffers.
  desc.BufferCount        = 2;
  desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferDesc.Width   = width_;
  desc.BufferDesc.Height  = height_;
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
    exit(1);
    return;
  }

  // Create a render target view to draw into.
  ID3D11Texture2D* back_buffer = 0;
  hr = swap_chain_->GetBuffer(
      0, __uuidof(ID3D11Texture2D), (void**)&back_buffer);
  if (FAILED(hr)) {
    XELOGE("GetBuffer (back_buffer) failed with %.8X", hr);
    exit(1);
    return;
  }
  hr = device_->CreateRenderTargetView(
      back_buffer, NULL, &render_target_view_);
  back_buffer->Release();
  if (FAILED(hr)) {
    XELOGE("CreateRenderTargetView (back_buffer) failed with %.8X", hr);
    exit(1);
    return;
  }
  context_->OMSetRenderTargets(1, &render_target_view_, NULL);
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

void D3D11Window::Swap() {
  // Setup the viewport.
  //D3D11_VIEWPORT viewport;
  //viewport.MinDepth = 0.0f;
  //viewport.MaxDepth = 1.0f;
  //viewport.TopLeftX = 0;
  //viewport.TopLeftY = 0;
  //viewport.Width    = (FLOAT)width_;
  //viewport.Height   = (FLOAT)height_;
  //context_->RSSetViewports(1, &viewport);

  // Swap buffers.
  // TODO(benvanik): control vsync with flag.
  bool vsync = true;
  HRESULT hr = swap_chain_->Present(vsync ? 1 : 0, 0);
  if (FAILED(hr)) {
    XELOGE("Present failed with %.8X", hr);
  }
}

void D3D11Window::OnResize(uint32_t width, uint32_t height) {
  Win32Window::OnResize(width, height);

  // TODO(benvanik): resize swap buffers?
}

void D3D11Window::OnClose() {
  // We are the master window - if they close us, quit!
  xe_run_loop_quit(run_loop_);
}
