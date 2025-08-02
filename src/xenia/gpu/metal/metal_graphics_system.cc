/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_graphics_system.h"

#include <algorithm>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/xbox.h"

namespace xe {
namespace gpu {
namespace metal {

MetalGraphicsSystem::MetalGraphicsSystem() {}

MetalGraphicsSystem::~MetalGraphicsSystem() {
  // Release Metal resources
  if (metal_command_queue_) {
    metal_command_queue_->release();
    metal_command_queue_ = nullptr;
  }
  if (metal_device_) {
    metal_device_->release();
    metal_device_ = nullptr;
  }
}

bool MetalGraphicsSystem::IsAvailable() {
  // Check for Metal device availability
  MTL::Device* device = MTL::CreateSystemDefaultDevice();
  bool available = (device != nullptr);
  if (device) {
    device->release();
  }
  return available;
}

std::string MetalGraphicsSystem::name() const {
  auto metal_command_processor =
      static_cast<MetalCommandProcessor*>(command_processor());
  if (metal_command_processor != nullptr) {
    return metal_command_processor->GetWindowTitleText();
  }
  return "Metal - EARLY DEVELOPMENT";
}

X_STATUS MetalGraphicsSystem::Setup(cpu::Processor* processor,
                                    kernel::KernelState* kernel_state,
                                    ui::WindowedAppContext* app_context,
                                    bool is_surface_required) {
  // Create Metal device
  metal_device_ = MTL::CreateSystemDefaultDevice();
  if (!metal_device_) {
    XELOGE("Failed to create Metal device");
    return X_STATUS_UNSUCCESSFUL;
  }
  
  // Create command queue
  metal_command_queue_ = metal_device_->newCommandQueue();
  if (!metal_command_queue_) {
    XELOGE("Failed to create Metal command queue");
    return X_STATUS_UNSUCCESSFUL;
  }
  
  XELOGI("Metal device created: {}", metal_device_->name()->utf8String());
  
  return GraphicsSystem::Setup(processor, kernel_state, app_context,
                               is_surface_required);
}

std::unique_ptr<CommandProcessor>
MetalGraphicsSystem::CreateCommandProcessor() {
  return std::unique_ptr<CommandProcessor>(
      new MetalCommandProcessor(this, kernel_state_));
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
