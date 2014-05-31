/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/gpu/d3d11/d3d11_graphics_system.h>

#include <xenia/emulator.h>
#include <xenia/gpu/gpu-private.h>
#include <xenia/gpu/d3d11/d3d11_graphics_driver.h>
#include <xenia/gpu/d3d11/d3d11_window.h>


using namespace xe;
using namespace xe::gpu;
using namespace xe::gpu::d3d11;


namespace {

void __stdcall D3D11GraphicsSystemVsyncCallback(
    D3D11GraphicsSystem* gs, BOOLEAN) {
  static bool thread_name_set = false;
  if (!thread_name_set) {
    thread_name_set = true;
    Profiler::ThreadEnter("VsyncTimer");
  }
  SCOPE_profile_cpu_f("gpu");

  gs->MarkVblank();
  gs->DispatchInterruptCallback(0);
}

}


D3D11GraphicsSystem::D3D11GraphicsSystem(Emulator* emulator) :
    window_(0), dxgi_factory_(0), device_(0),
    timer_queue_(NULL), vsync_timer_(NULL),
    GraphicsSystem(emulator) {
}

D3D11GraphicsSystem::~D3D11GraphicsSystem() {
}

void D3D11GraphicsSystem::Initialize() {
  GraphicsSystem::Initialize();

  XEASSERTNULL(timer_queue_);
  XEASSERTNULL(vsync_timer_);

  timer_queue_ = CreateTimerQueue();
  CreateTimerQueueTimer(
      &vsync_timer_,
      timer_queue_,
      (WAITORTIMERCALLBACK)D3D11GraphicsSystemVsyncCallback,
      this,
      16,
      16,
      WT_EXECUTEINTIMERTHREAD);

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
  if (window_->Initialize("Xenia D3D11", 1280, 720)) {
    XELOGE("Failed to create D3D11Window");
    exit(1);
    return;
  }
  emulator_->set_main_window(window_);

  // Listen for alt-enter/etc.
  dxgi_factory_->MakeWindowAssociation(window_->handle(), 0);

  // Create the driver.
  // This runs in the worker thread and builds command lines to present
  // in the window.
  XEASSERTNULL(driver_);
  driver_ = new D3D11GraphicsDriver(
      memory_, window_->swap_chain(), device_);

  // Initial vsync kick.
  DispatchInterruptCallback(0);
}

void D3D11GraphicsSystem::Pump() {
  if (swap_pending_) {
    swap_pending_ = false;

    // TODO(benvanik): remove this when commands are understood.
    driver_->Resolve();

    // Swap window.
    // If we are set to vsync this will block.
    window_->Swap();

    DispatchInterruptCallback(0);
  } else {
    double time_since_last_interrupt = xe_pal_now() - last_interrupt_time_;
    if (time_since_last_interrupt > 0.5) {
      // If we have gone too long without an interrupt, fire one.
      DispatchInterruptCallback(0);
    }
    if (time_since_last_interrupt > 0.3) {
      // Force a swap when profiling.
      if (Profiler::is_enabled()) {
        window_->Swap();
      }
    }
  }
}

void D3D11GraphicsSystem::Shutdown() {
  GraphicsSystem::Shutdown();
  
  if (vsync_timer_) {
    DeleteTimerQueueTimer(timer_queue_, vsync_timer_, NULL);
  }
  if (timer_queue_) {
    DeleteTimerQueueEx(timer_queue_, NULL);
  }

  XESAFERELEASE(device_);
  device_ = 0;
  XESAFERELEASE(dxgi_factory_);
  dxgi_factory_ = 0;
  delete window_;
  window_ = 0;
}
