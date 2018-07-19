/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_D3D12_D3D12_PROVIDER_H_
#define XENIA_UI_D3D12_D3D12_PROVIDER_H_

#include <memory>

#include "xenia/ui/d3d12/d3d12_api.h"
#include "xenia/ui/graphics_provider.h"

namespace xe {
namespace ui {
namespace d3d12 {

class D3D12Provider : public GraphicsProvider {
 public:
  ~D3D12Provider() override;

  static std::unique_ptr<D3D12Provider> Create(Window* main_window);

  std::unique_ptr<GraphicsContext> CreateContext(
      Window* target_window) override;
  std::unique_ptr<GraphicsContext> CreateOffscreenContext() override;

  IDXGIFactory2* get_dxgi_factory() const { return dxgi_factory_; }
  ID3D12Device* get_device() const { return device_; }
  ID3D12CommandQueue* get_direct_queue() const { return direct_queue_; }

  uint32_t get_descriptor_size_view() const { return descriptor_size_view_; }
  uint32_t get_descriptor_size_sampler() const {
    return descriptor_size_sampler_;
  }
  uint32_t get_descriptor_size_rtv() const { return descriptor_size_rtv_; }
  uint32_t get_descriptor_size_dsv() const { return descriptor_size_dsv_; }

 private:
  explicit D3D12Provider(Window* main_window);

  bool Initialize();
  static bool IsDeviceSupported(ID3D12Device* device);

  IDXGIFactory2* dxgi_factory_ = nullptr;
  ID3D12Device* device_ = nullptr;
  ID3D12CommandQueue* direct_queue_ = nullptr;

  uint32_t descriptor_size_view_;
  uint32_t descriptor_size_sampler_;
  uint32_t descriptor_size_rtv_;
  uint32_t descriptor_size_dsv_;
};

}  // namespace d3d12
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_D3D12_D3D12_PROVIDER_H_
