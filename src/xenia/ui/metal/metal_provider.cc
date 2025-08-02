/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/metal/metal_provider.h"

#include "Metal/Metal.hpp"

#include "xenia/base/logging.h"
#include "xenia/ui/metal/metal_immediate_drawer.h"
#include "xenia/ui/metal/metal_presenter.h"

namespace xe {
namespace ui {
namespace metal {

std::unique_ptr<MetalProvider> MetalProvider::Create() {
  auto provider = std::unique_ptr<MetalProvider>(new MetalProvider());
  if (!provider->Initialize()) {
    return nullptr;
  }
  return provider;
}

MetalProvider::MetalProvider() = default;

MetalProvider::~MetalProvider() {
  if (device_) {
    device_->release();
  }
}

bool MetalProvider::Initialize() {
  // Create default Metal device
  device_ = MTL::CreateSystemDefaultDevice();
  if (!device_) {
    XELOGE("Failed to create Metal device");
    return false;
  }

  XELOGI("Metal device created: {}", device_->name()->utf8String());
  return true;
}

std::unique_ptr<Presenter> MetalProvider::CreatePresenter(
    Presenter::HostGpuLossCallback host_gpu_loss_callback) {
  auto presenter = std::make_unique<MetalPresenter>(this, host_gpu_loss_callback);
  if (!presenter->Initialize()) {
    XELOGE("Metal presenter failed to initialize");
    return nullptr;
  }
  return std::move(presenter);
}

std::unique_ptr<ImmediateDrawer> MetalProvider::CreateImmediateDrawer() {
  auto drawer = std::make_unique<MetalImmediateDrawer>(this);
  if (!drawer->Initialize()) {
    XELOGE("Metal immediate drawer failed to initialize");
    return nullptr;
  }
  return std::move(drawer);
}

}  // namespace metal
}  // namespace ui
}  // namespace xe
