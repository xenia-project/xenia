/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_METAL_METAL_PRESENTER_H_
#define XENIA_GPU_METAL_METAL_PRESENTER_H_

#include <memory>

#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/surface.h"

#include "third_party/metal-cpp/Metal/Metal.hpp"
#include "third_party/metal-cpp/QuartzCore/QuartzCore.hpp"

namespace xe {
namespace gpu {
namespace metal {

class MetalCommandProcessor;

class MetalPresenter : public xe::ui::Presenter {
 public:
  MetalPresenter(HostGpuLossCallback host_gpu_loss_callback,
                 MetalCommandProcessor* command_processor);
  ~MetalPresenter() override;

  // Presenter implementation
  xe::ui::Surface::TypeFlags GetSupportedSurfaceTypes() const override;
  bool CaptureGuestOutput(xe::ui::RawImage& image_out) override;

  // ImmediateDrawer access
  xe::ui::ImmediateDrawer* immediate_drawer();

  // Custom Metal presenter methods
  bool Initialize();
  void Shutdown();

  // Frame management
  bool BeginFrame();
  void EndFrame(bool present = true);
  
  // Metal-specific presentation
  void Present(MTL::Texture* source_texture);
  
  // Set up Metal layer for presentation
  bool SetupMetalLayer(void* layer);
  
  // Get Metal command queue for presentation
  MTL::CommandQueue* GetCommandQueue() const { return command_queue_; }
  
  // Check if frame is in progress
  bool IsFrameInProgress() const { return frame_begun_; }

 protected:
  // Presenter implementation
  void DisconnectPaintingFromSurfaceFromUIThreadImpl() override;
  xe::ui::Presenter::PaintResult PaintAndPresentImpl(bool execute_ui_drawers) override;

 private:
  // Metal presentation state
  MetalCommandProcessor* command_processor_;
  MTL::Device* device_;
  MTL::CommandQueue* command_queue_;
  CA::MetalLayer* metal_layer_;
  
  // Presentation resources
  MTL::RenderPipelineState* blit_pipeline_;
  MTL::Buffer* fullscreen_quad_buffer_;
  
  // Frame synchronization
  bool frame_begun_;
  CA::MetalDrawable* current_drawable_;
  MTL::CommandBuffer* current_command_buffer_;
  
  // Helper methods
  bool CreateBlitPipeline();
  bool CreateFullscreenQuadBuffer();
  void BlitTexture(MTL::Texture* source, MTL::Texture* destination);
};

}  // namespace metal
}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_METAL_METAL_PRESENTER_H_
