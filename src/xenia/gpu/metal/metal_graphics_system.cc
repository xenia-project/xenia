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
#include "xenia/ui/presenter.h"
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
  
#if defined(DEBUG) || defined(_DEBUG) || defined(CHECKED)
  // Enable Metal validation layers for debug/checked builds
  // This helps catch API misuse, resource leaks, and other issues
  setenv("METAL_DEVICE_WRAPPER_TYPE", "1", 1);  // Enable Metal validation
  setenv("METAL_DEBUG_ERROR_MODE", "5", 1);      // Assert on errors
  setenv("METAL_SHADER_VALIDATION", "1", 1);     // Enable shader validation
  
  XELOGI("Metal validation layers ENABLED for debug/checked build");
#endif
  
  // Create command queue with error handler
  metal_command_queue_ = metal_device_->newCommandQueue();
  if (!metal_command_queue_) {
    XELOGE("Failed to create Metal command queue");
    return X_STATUS_UNSUCCESSFUL;
  }
  
  // Set a label for debugging
  metal_command_queue_->setLabel(NS::String::string("Xenia Xbox 360 GPU Command Queue", NS::UTF8StringEncoding));
  
  XELOGI("Metal device created: {}", metal_device_->name()->utf8String());
  XELOGI("Metal device supports: Unified Memory={}, GPU Family={}",
         metal_device_->hasUnifiedMemory() ? "Yes" : "No",
         metal_device_->supportsFamily(MTL::GPUFamilyApple7) ? "Apple7+" : "Earlier");
  
  return GraphicsSystem::Setup(processor, kernel_state, app_context,
                               is_surface_required);
}

std::unique_ptr<CommandProcessor>
MetalGraphicsSystem::CreateCommandProcessor() {
  return std::unique_ptr<CommandProcessor>(
      new MetalCommandProcessor(this, kernel_state_));
}

bool MetalGraphicsSystem::CaptureGuestOutput(ui::RawImage& raw_image) {
  XELOGI("Metal CaptureGuestOutput: Called, presenter={}", presenter() ? "yes" : "no");
  
  // Don't use presenter even if set - it might be our trace dump presenter
  // which would cause infinite recursion. Always go directly to command processor.
  
  // Use last captured frame from the command processor
  auto* metal_cmd_processor = static_cast<MetalCommandProcessor*>(command_processor());
  if (!metal_cmd_processor) {
    XELOGE("Metal CaptureGuestOutput: No command processor available");
    return false;
  }
  
  XELOGI("Metal CaptureGuestOutput: Using command processor");
  uint32_t width, height;
  std::vector<uint8_t> data;
  
  // Try to get the last captured frame first (this works even after shutdown)
  if (!metal_cmd_processor->GetLastCapturedFrame(width, height, data)) {
    XELOGW("Metal CaptureGuestOutput: No last captured frame, trying live capture");
    // Fallback to live capture if no frame was saved
    if (!metal_cmd_processor->CaptureColorTarget(0, width, height, data)) {
      XELOGE("Metal CaptureGuestOutput: Failed to get captured frame");
      return false;
    }
  }
  
  // Set up the raw image
  raw_image.width = width;
  raw_image.height = height;
  raw_image.stride = width * 4;  // RGBA8
  raw_image.data = std::move(data);
  
  XELOGI("Metal CaptureGuestOutput: Captured {}x{} image", width, height);
  return true;
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
