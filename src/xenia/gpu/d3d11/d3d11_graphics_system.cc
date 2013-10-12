/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_graphics_system.h>

#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/d3d11/d3d11_graphics_driver.h>
#include <xenia/gpu/d3d11/d3d11_window.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;


namespace {

}


D3D11GraphicsSystem::D3D11GraphicsSystem(const CreationParams* params) :
    window_(0), dxgi_factory_(0), device_(0),
    GraphicsSystem(params) {
}

D3D11GraphicsSystem::~D3D11GraphicsSystem() {
  XESAFERELEASE(device_);
  XESAFERELEASE(dxgi_factory_);
  delete window_;
}

void D3D11GraphicsSystem::Initialize() {
  GraphicsSystem::Initialize();

  // Create DXGI factory so we can get a swap chain/etc.
  HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                  (void**)&dxgi_factory_);
  if (FAILED(hr)) {
    XELOGE("CreateDXGIFactory1 failed with %.8X", hr);
    exit(1);
    return;
  }

  // Find the best adapter.
  // TODO(benvanik): enable nvperfhud/etc.
  IDXGIAdapter1* adapter = 0;
  UINT n = 0;
  while (dxgi_factory_->EnumAdapters1(n, &adapter) != DXGI_ERROR_NOT_FOUND) {
    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);
    adapter->Release();
    n++;
  }
  // Just go with the default for now.
  adapter = 0;
  
  D3D_DRIVER_TYPE driver_type =
      adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

  UINT flags = 0;
  // TODO(benvanik): runtime flag
  flags |= D3D11_CREATE_DEVICE_DEBUG;

  // Feature level 11.0+ only.
  D3D_FEATURE_LEVEL feature_levels[] = {
    D3D_FEATURE_LEVEL_11_0,
  };

  // Create device.
  D3D_FEATURE_LEVEL actual_feature_level;
  ID3D11DeviceContext* immediate_context;
  hr = D3D11CreateDevice(
      adapter,
      driver_type,
      0, // software driver HMODULE
      flags,
      feature_levels,
      XECOUNT(feature_levels),
      D3D11_SDK_VERSION,
      &device_,
      &actual_feature_level,
      &immediate_context);
  if (adapter) {
    adapter->Release();
    adapter = 0;
  }
  if (immediate_context) {
    immediate_context->Release();
    immediate_context = 0;
  }
  if (FAILED(hr)) {
    XELOGE("D3D11CreateDevice failed with %.8X", hr);
    exit(1);
    return;
  }

  // Create the window.
  // This will pump through the run-loop and and be where our swapping
  // will take place.
  XEASSERTNULL(window_);
  window_ = new D3D11Window(run_loop_, dxgi_factory_, device_);
  window_->set_title(XETEXT("Xenia D3D11"));

  // Listen for alt-enter/etc.
  dxgi_factory_->MakeWindowAssociation(window_->handle(), 0);

  // Create the driver.
  // This runs in the worker thread and builds command lines to present
  // in the window.
  XEASSERTNULL(driver_);
  driver_ = new D3D11GraphicsDriver(memory_, device_);

  // Initial vsync kick.
  DispatchInterruptCallback();
}

void D3D11GraphicsSystem::Pump() {
  if (swap_pending_) {
    swap_pending_ = false;

    // Swap window.
    // If we are set to vsync this will block.
    window_->Swap();

    // Dispatch interrupt callback to let the game know it can keep drawing.
    DispatchInterruptCallback();
  }
}

void D3D11GraphicsSystem::Shutdown() {
  GraphicsSystem::Shutdown();
}
