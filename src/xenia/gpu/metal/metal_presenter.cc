/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_presenter.h"

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/ui/surface.h"

namespace xe {
namespace gpu {
namespace metal {

MetalPresenter::MetalPresenter(HostGpuLossCallback host_gpu_loss_callback,
                               MetalCommandProcessor* command_processor)
    : xe::ui::Presenter(std::move(host_gpu_loss_callback)) {
#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  command_processor_ = command_processor;
  device_ = nullptr;
  command_queue_ = nullptr;
  metal_layer_ = nullptr;
  blit_pipeline_ = nullptr;
  fullscreen_quad_buffer_ = nullptr;
  frame_begun_ = false;
  current_drawable_ = nullptr;
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE
}

MetalPresenter::~MetalPresenter() {
  Shutdown();
}

bool MetalPresenter::Initialize() {
  SCOPE_profile_cpu_f("gpu");

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  // TODO: Get Metal device from command processor
  // device_ = command_processor_->GetMetalDevice();
  // For now, create a system default device
  device_ = MTL::CreateSystemDefaultDevice();
  if (!device_) {
    XELOGE("Metal presenter: Failed to get Metal device");
    return false;
  }

  // Create command queue
  command_queue_ = device_->newCommandQueue();
  if (!command_queue_) {
    XELOGE("Metal presenter: Failed to create command queue");
    return false;
  }

  // Create presentation resources
  if (!CreateBlitPipeline()) {
    XELOGE("Metal presenter: Failed to create blit pipeline");
    return false;
  }

  if (!CreateFullscreenQuadBuffer()) {
    XELOGE("Metal presenter: Failed to create fullscreen quad buffer");
    return false;
  }
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

  XELOGD("Metal presenter: Initialized successfully");
  return true;
}

void MetalPresenter::Shutdown() {
  SCOPE_profile_cpu_f("gpu");

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  if (current_drawable_) {
    current_drawable_->release();
    current_drawable_ = nullptr;
  }

  if (fullscreen_quad_buffer_) {
    fullscreen_quad_buffer_->release();
    fullscreen_quad_buffer_ = nullptr;
  }

  if (blit_pipeline_) {
    blit_pipeline_->release();
    blit_pipeline_ = nullptr;
  }

  if (command_queue_) {
    command_queue_->release();
    command_queue_ = nullptr;
  }

  if (device_) {
    device_->release();
    device_ = nullptr;
  }

  if (metal_layer_) {
    metal_layer_->release();
    metal_layer_ = nullptr;
  }

  frame_begun_ = false;
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

  XELOGD("Metal presenter: Shutdown complete");
}

xe::ui::Surface::TypeFlags MetalPresenter::GetSupportedSurfaceTypes() const {
#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  return xe::ui::Surface::kTypeFlag_MacNSView;
#else
  return 0;
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE
}

bool MetalPresenter::CaptureGuestOutput(xe::ui::RawImage& image_out) {
  // TODO: Implement frame capture for Metal
  return false;
}

void MetalPresenter::DisconnectPaintingFromSurfaceFromUIThreadImpl() {
  // TODO: Implement surface disconnection
}

xe::ui::Presenter::PaintResult MetalPresenter::PaintAndPresentImpl(bool execute_ui_drawers) {
  // TODO: Implement actual painting and presentation
  return xe::ui::Presenter::PaintResult::kNotPresented;
}

xe::ui::ImmediateDrawer* MetalPresenter::immediate_drawer() {
  // TODO: Implement Metal immediate drawer
  return nullptr;
}

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)

void MetalPresenter::Present(MTL::Texture* source_texture) {
  SCOPE_profile_cpu_f("gpu");

  if (!frame_begun_ || !current_drawable_ || !source_texture) {
    return;
  }

  // Get the drawable's texture
  MTL::Texture* drawable_texture = current_drawable_->texture();
  if (!drawable_texture) {
    return;
  }

  // Blit the source texture to the drawable texture
  BlitTexture(source_texture, drawable_texture);
}

bool MetalPresenter::SetupMetalLayer(void* layer) {
  if (!layer) {
    return false;
  }

  // Cast the layer to CA::MetalLayer
  metal_layer_ = static_cast<CA::MetalLayer*>(layer);
  
  if (device_) {
    metal_layer_->setDevice(device_);
  }
  
  // Set up layer properties
  metal_layer_->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
  metal_layer_->setFramebufferOnly(true);
  
  return true;
}

bool MetalPresenter::CreateBlitPipeline() {
  // TODO: Create a proper blit pipeline with vertex and fragment shaders
  // For now, return true as a stub
  XELOGD("Metal presenter: Blit pipeline creation stubbed");
  return true;
}

bool MetalPresenter::CreateFullscreenQuadBuffer() {
  // Fullscreen quad vertices (position + texture coordinates)
  float quad_vertices[] = {
    // positions    // texture coords
    -1.0f, -1.0f,   0.0f, 1.0f,  // bottom left
     1.0f, -1.0f,   1.0f, 1.0f,  // bottom right
    -1.0f,  1.0f,   0.0f, 0.0f,  // top left
     1.0f,  1.0f,   1.0f, 0.0f,  // top right
  };

  fullscreen_quad_buffer_ = device_->newBuffer(
      quad_vertices, sizeof(quad_vertices), 
      MTL::ResourceStorageModeShared);

  if (!fullscreen_quad_buffer_) {
    XELOGE("Metal presenter: Failed to create fullscreen quad buffer");
    return false;
  }

  return true;
}

void MetalPresenter::BlitTexture(MTL::Texture* source, MTL::Texture* destination) {
  // TODO: Implement proper texture blitting using Metal render commands
  // This would typically involve:
  // 1. Creating a render command encoder
  // 2. Setting up the blit pipeline
  // 3. Binding the source texture as input
  // 4. Drawing the fullscreen quad to the destination
  
  XELOGD("Metal presenter: Texture blit operation stubbed");
}

#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

}  // namespace metal
}  // namespace gpu
}  // namespace xe
