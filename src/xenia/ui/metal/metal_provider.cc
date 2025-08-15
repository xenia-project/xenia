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

bool MetalProvider::IsMetalAPIAvailable() {
  MTL::Device* device = MTL::CreateSystemDefaultDevice();
  bool available = (device != nullptr);
  if (device) {
    device->release();
  }
  return available;
}

std::unique_ptr<MetalProvider> MetalProvider::Create() {
  auto provider = std::unique_ptr<MetalProvider>(new MetalProvider());
  if (!provider->Initialize()) {
    xe::FatalError(
      "Unable to Initialize Metal Graphics Subsystem.\n"
      "\n"
      "Ensure you have the latest OS your device supports\n"
    );
    return nullptr;
  }
  return provider;
}

MetalProvider::MetalProvider() = default;

MetalProvider::~MetalProvider() {
  if (command_queue_) {
    command_queue_->release();
  }
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
  command_queue_ = device_->newCommandQueue();
  if (!command_queue_) {
    XELOGE("Failed to create Metal Command Queue");
    return false;
  }

    // Enable Metal validation layer in Debug and Checked builds
  #if !defined(NDEBUG) || defined(MTL_DEBUG_LAYER)
    // Set environment variable for Metal validation
    setenv("METAL_DEVICE_WRAPPER_TYPE", "1", 1);
    setenv("METAL_DEBUG_ERROR_MODE", "assert", 1);
    XELOGI("Metal validation layer enabled");
  #endif
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
