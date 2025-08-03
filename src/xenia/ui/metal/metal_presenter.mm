/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/metal/metal_presenter.h"

#include "Metal/Metal.hpp"

#include "xenia/base/logging.h"
#include "xenia/ui/metal/metal_provider.h"
#include "xenia/ui/surface_mac.h"

// Objective-C imports for Metal layer configuration
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
namespace xe {
namespace ui {
namespace metal {

MetalPresenter::MetalPresenter(MetalProvider* provider,
                               HostGpuLossCallback host_gpu_loss_callback)
    : Presenter(host_gpu_loss_callback), provider_(provider) {
  device_ = provider_->GetDevice();
}

MetalPresenter::~MetalPresenter() = default;

bool MetalPresenter::Initialize() {
  // Create Metal command queue
  command_queue_ = [(__bridge id<MTLDevice>)device_ newCommandQueue];
  if (!command_queue_) {
    XELOGE("Metal presenter failed to create command queue");
    return false;
  }
  
  XELOGI("Metal presenter initialized with command queue");
  return true;
}

void MetalPresenter::Shutdown() {
  // Release Metal presentation resources
  if (command_queue_) {
    command_queue_ = nullptr;
  }
  metal_layer_ = nullptr;
  XELOGI("Metal presenter shut down");
}

Surface::TypeFlags MetalPresenter::GetSupportedSurfaceTypes() const {
  // TODO(wmarti): Return actual supported surface types for Metal
  return Surface::kTypeFlag_MacNSView;
}

bool MetalPresenter::CaptureGuestOutput(RawImage& image_out) {
  // TODO(wmarti): Implement Metal guest output capture
  XELOGW("Metal CaptureGuestOutput not implemented");
  return false;
}

Presenter::PaintResult MetalPresenter::PaintAndPresentImpl(bool execute_ui_drawers) {
  if (!metal_layer_) {
    XELOGW("Metal PaintAndPresentImpl called without connected surface");
    return PaintResult::kNotPresented;
  }
  
  if (!command_queue_) {
    XELOGW("Metal PaintAndPresentImpl called without command queue");
    return PaintResult::kNotPresented;
  }
  
  // Get the next drawable from the CAMetalLayer
  id<CAMetalDrawable> drawable = [metal_layer_ nextDrawable];
  if (!drawable) {
    XELOGW("Metal PaintAndPresentImpl failed to get drawable");
    return PaintResult::kNotPresented;
  }
  
  // Create command buffer
  id<MTLCommandBuffer> command_buffer = [command_queue_ commandBuffer];
  
  // Create render pass descriptor
  MTLRenderPassDescriptor* render_pass_desc = [MTLRenderPassDescriptor renderPassDescriptor];
  render_pass_desc.colorAttachments[0].texture = drawable.texture;
  render_pass_desc.colorAttachments[0].loadAction = MTLLoadActionClear;
  render_pass_desc.colorAttachments[0].storeAction = MTLStoreActionStore;
  render_pass_desc.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0); // Black background
  
  // Create render command encoder
  id<MTLRenderCommandEncoder> render_encoder = [command_buffer renderCommandEncoderWithDescriptor:render_pass_desc];
  
  // Execute UI drawers if requested
  if (execute_ui_drawers) {
    // Create Metal UI draw context
    MetalUIDrawContext metal_ui_draw_context(
        *this, 
        static_cast<uint32_t>(drawable.texture.width),
        static_cast<uint32_t>(drawable.texture.height),
        command_buffer,
        render_encoder);
    
    // Execute the UI drawers
    ExecuteUIDrawersFromUIThread(metal_ui_draw_context);
    // XELOGI("Metal PaintAndPresentImpl: UI drawers executed successfully");
  }
  
  // End rendering
  [render_encoder endEncoding];
  
  // Present the drawable
  [command_buffer presentDrawable:drawable];
  [command_buffer commit];
  
  // XELOGI("Metal PaintAndPresentImpl: Frame presented successfully");
  return PaintResult::kPresented;
}

Presenter::SurfacePaintConnectResult 
MetalPresenter::ConnectOrReconnectPaintingToSurfaceFromUIThread(
    Surface& new_surface, uint32_t new_surface_width,
    uint32_t new_surface_height, bool was_paintable,
    bool& is_vsync_implicit_out) {
  
  // Check if this is a supported surface type
  Surface::TypeIndex surface_type = new_surface.GetType();
  if (!(GetSupportedSurfaceTypes() & (Surface::TypeFlags(1) << surface_type))) {
    XELOGE("Metal surface type not supported: {}", surface_type);
    return SurfacePaintConnectResult::kFailure;
  }
  
  // For macOS, we expect a MacNSView surface
  if (surface_type != Surface::kTypeIndex_MacNSView) {
    XELOGE("Metal presenter requires MacNSView surface, got: {}", surface_type);
    return SurfacePaintConnectResult::kFailure;
  }
  
  // Cast to MacNSViewSurface to get the CAMetalLayer
  MacNSViewSurface& mac_ns_view_surface = static_cast<MacNSViewSurface&>(new_surface);
  CAMetalLayer* metal_layer = mac_ns_view_surface.GetOrCreateMetalLayer();
  
  if (!metal_layer) {
    XELOGE("Metal presenter failed to get CAMetalLayer from surface");
    return SurfacePaintConnectResult::kFailure;
  }
  
  // Configure the metal layer for our device using Objective-C syntax
  metal_layer.device = (__bridge id<MTLDevice>)device_;
  metal_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  metal_layer.drawableSize = CGSizeMake(new_surface_width, new_surface_height);
  
  // Store the metal layer
  metal_layer_ = metal_layer;
  
  // Metal handles vsync through the CAMetalLayer
  is_vsync_implicit_out = true;
  
  XELOGI("Metal surface connected successfully: {}x{}", new_surface_width, new_surface_height);
  return SurfacePaintConnectResult::kSuccess;
}

void MetalPresenter::DisconnectPaintingFromSurfaceFromUIThreadImpl() {
  // Clean up Metal presentation resources
  if (metal_layer_) {
    metal_layer_ = nullptr;
  }
  
  // Command queue is owned by the provider, not released here
  command_queue_ = nullptr;
  
  XELOGI("Metal surface disconnected successfully");
}

bool MetalPresenter::RefreshGuestOutputImpl(
    uint32_t mailbox_index, uint32_t frontbuffer_width,
    uint32_t frontbuffer_height,
    std::function<bool(GuestOutputRefreshContext& context)> refresher,
    bool& is_8bpc_out_ref) {
  // TODO(wmarti): Implement Metal guest output refresh
  XELOGW("Metal RefreshGuestOutputImpl not implemented");
  is_8bpc_out_ref = false;
  return false;
}

}  // namespace metal
}  // namespace ui
}  // namespace xe
