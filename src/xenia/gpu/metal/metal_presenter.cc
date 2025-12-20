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

// Generated shaders
#include "xenia/ui/shaders/bytecode/metal/guest_output_bilinear_ps.h"
#include "xenia/ui/shaders/bytecode/metal/guest_output_triangle_strip_rect_vs.h"

namespace xe {
namespace gpu {
namespace metal {

MetalPresenter::MetalPresenter(HostGpuLossCallback host_gpu_loss_callback,
                               MetalCommandProcessor* command_processor)
    : xe::ui::Presenter(std::move(host_gpu_loss_callback)),
      command_processor_(command_processor),
      device_(nullptr),
      command_queue_(nullptr),
      metal_layer_(nullptr),
      blit_pipeline_(nullptr),
      fullscreen_quad_buffer_(nullptr),
      frame_begun_(false),
      current_drawable_(nullptr),
      current_command_buffer_(nullptr) {
}

MetalPresenter::~MetalPresenter() {
  Shutdown();
}

bool MetalPresenter::Initialize() {
  SCOPE_profile_cpu_f("gpu");

  // Get Metal device from command processor
  device_ = command_processor_->GetMetalDevice();
  if (!device_) {
    XELOGE("Metal presenter: Failed to get Metal device from command processor");
    return false;
  }

  // Create command queue for presentation
  command_queue_ = device_->newCommandQueue();
  if (!command_queue_) {
    XELOGE("Metal presenter: Failed to create command queue");
    device_->release();
    return false;
  }
  command_queue_->setLabel(NS::String::string("Presentation Command Queue", NS::UTF8StringEncoding));

  // Create presentation resources
  if (!CreateBlitPipeline()) {
    XELOGE("Metal presenter: Failed to create blit pipeline");
    return false;
  }

  XELOGD("Metal presenter: Initialized successfully");
  return true;
}

void MetalPresenter::Shutdown() {
  SCOPE_profile_cpu_f("gpu");

  // End any in-progress frame
  if (frame_begun_) {
    EndFrame(false);
  }

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

  // Note: We don't release metal_layer_ as it's owned by the UI layer
  metal_layer_ = nullptr;
  frame_begun_ = false;

  XELOGD("Metal presenter: Shutdown complete");
}

xe::ui::Surface::TypeFlags MetalPresenter::GetSupportedSurfaceTypes() const {
  return xe::ui::Surface::kTypeFlag_MacNSView;
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

bool MetalPresenter::BeginFrame() {
  SCOPE_profile_cpu_f("gpu");

  if (frame_begun_) {
    XELOGW("Metal presenter: BeginFrame called while frame already in progress");
    return false;
  }

  if (!metal_layer_) {
    return false;
  }

  // Get the next drawable from the Metal layer
  current_drawable_ = metal_layer_->nextDrawable();
  if (!current_drawable_) {
    XELOGW("Metal presenter: Failed to acquire next drawable");
    return false;
  }

  // Create command buffer for this frame
  current_command_buffer_ = command_queue_->commandBuffer();
  if (!current_command_buffer_) {
    XELOGE("Metal presenter: Failed to create command buffer");
    current_drawable_->release();
    current_drawable_ = nullptr;
    return false;
  }
  current_command_buffer_->setLabel(NS::String::string("Presentation Frame", NS::UTF8StringEncoding));

  frame_begun_ = true;
  return true;
}

void MetalPresenter::EndFrame(bool present) {
  SCOPE_profile_cpu_f("gpu");

  if (!frame_begun_) {
    return;
  }

  if (present && current_drawable_ && current_command_buffer_) {
    // Present the drawable
    current_command_buffer_->presentDrawable(current_drawable_);
    
    // Commit the command buffer
    current_command_buffer_->commit();
  }

  // Clean up frame resources
  if (current_command_buffer_) {
    current_command_buffer_->release();
    current_command_buffer_ = nullptr;
  }

  if (current_drawable_) {
    current_drawable_->release();
    current_drawable_ = nullptr;
  }

  frame_begun_ = false;
}

void MetalPresenter::Present(MTL::Texture* source_texture) {
  SCOPE_profile_cpu_f("gpu");

  if (!frame_begun_ || !current_drawable_ || !source_texture || !current_command_buffer_) {
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
  
  // Set up layer properties for optimal Xbox 360 rendering
  metal_layer_->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
  metal_layer_->setFramebufferOnly(false);  // Allow reading for debug captures
  metal_layer_->setDisplaySyncEnabled(true);  // VSync
  metal_layer_->setMaximumDrawableCount(3);  // Triple buffering
  
  return true;
}

MTL::Function* CreateFunction(MTL::Device* device, const void* data, size_t size,
                              const char* name) {
  dispatch_data_t d = dispatch_data_create(data, size, nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  NS::Error* error = nullptr;
  MTL::Library* lib = device->newLibrary(d, &error);
  dispatch_release(d);
  if (!lib) {
    XELOGE("Metal presenter: Failed to create library for {}: {}", name,
           error ? error->localizedDescription()->utf8String() : "unknown");
    return nullptr;
  }
  NS::String* ns_name = NS::String::string("xesl_entry", NS::UTF8StringEncoding);
  MTL::Function* fn = lib->newFunction(ns_name);
  lib->release();
  if (!fn) {
    XELOGE("Metal presenter: Failed to load function xesl_entry for {}", name);
  }
  return fn;
}

bool MetalPresenter::CreateBlitPipeline() {
  MTL::Function* vs = CreateFunction(device_, guest_output_triangle_strip_rect_vs_metallib,
                                     sizeof(guest_output_triangle_strip_rect_vs_metallib),
                                     "blit_vs");
  if (!vs) return false;

  MTL::Function* ps = CreateFunction(device_, guest_output_bilinear_ps_metallib,
                                     sizeof(guest_output_bilinear_ps_metallib),
                                     "blit_ps");
  if (!ps) {
    vs->release();
    return false;
  }

  MTL::RenderPipelineDescriptor* desc = MTL::RenderPipelineDescriptor::alloc()->init();
  desc->setVertexFunction(vs);
  desc->setFragmentFunction(ps);
  // Match MetalLayer pixel format
  desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB);
  desc->setDepthAttachmentPixelFormat(MTL::PixelFormatInvalid);
  
  NS::Error* error = nullptr;
  blit_pipeline_ = device_->newRenderPipelineState(desc, &error);
  desc->release();
  vs->release();
  ps->release();

  if (!blit_pipeline_) {
    XELOGE("Metal presenter: Failed to create pipeline: {}",
           error ? error->localizedDescription()->utf8String() : "unknown");
    return false;
  }

  return true;
}

bool MetalPresenter::CreateFullscreenQuadBuffer() {
  // Deprecated: VS generates vertices.
  return true;
}

void MetalPresenter::BlitTexture(MTL::Texture* source, MTL::Texture* destination) {
  if (!current_command_buffer_ || !source || !destination) {
    return;
  }

  // Always use the render pass with blit pipeline to handle format conversion,
  // scaling, and gamma correction properly.
  // (The previous blit-copy optimization is removed because it bypasses gamma/scaling).

  MTL::RenderPassDescriptor* render_pass = MTL::RenderPassDescriptor::alloc()->init();
  if (!render_pass) {
    return;
  }

  MTL::RenderPassColorAttachmentDescriptor* color_attachment = 
      render_pass->colorAttachments()->object(0);
  color_attachment->setTexture(destination);
  color_attachment->setLoadAction(MTL::LoadActionDontCare);
  color_attachment->setStoreAction(MTL::StoreActionStore);

  MTL::RenderCommandEncoder* render_encoder = 
      current_command_buffer_->renderCommandEncoder(render_pass);
  
  if (render_encoder) {
    render_encoder->setLabel(NS::String::string("Present Render", NS::UTF8StringEncoding));
    
    if (blit_pipeline_) {
      render_encoder->setRenderPipelineState(blit_pipeline_);
      
      // Push Constants for VS: Offset(-1,-1), Size(2,2) for full screen NDC
      struct {
        float offset[2];
        float size[2];
      } constants = {{-1.0f, -1.0f}, {2.0f, 2.0f}};
      
      render_encoder->setVertexBytes(&constants, sizeof(constants), 0);
      render_encoder->setFragmentTexture(source, 0);
      
      // Draw 4 vertices (triangle strip)
      render_encoder->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(4));
    }
    
    render_encoder->endEncoding();
  }
  
  render_pass->release();
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
