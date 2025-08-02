/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_METAL_METAL_PROVIDER_H_
#define XENIA_UI_METAL_METAL_PROVIDER_H_

#include <memory>

#include "xenia/ui/graphics_provider.h"

// Forward declarations for Metal types
namespace MTL {
class Device;
}

namespace xe {
namespace ui {
namespace metal {

class MetalProvider : public GraphicsProvider {
 public:
  static std::unique_ptr<MetalProvider> Create();

  ~MetalProvider() override;

  std::unique_ptr<Presenter> CreatePresenter(
      Presenter::HostGpuLossCallback host_gpu_loss_callback =
          Presenter::FatalErrorHostGpuLossCallback) override;

  std::unique_ptr<ImmediateDrawer> CreateImmediateDrawer() override;

  MTL::Device* GetDevice() const { return device_; }

 private:
  MetalProvider();

  bool Initialize();

  MTL::Device* device_ = nullptr;
};

}  // namespace metal
}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_METAL_METAL_PROVIDER_H_
