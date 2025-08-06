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

  if (!CreateFullscreenQuadBuffer()) {
    XELOGE("Metal presenter: Failed to create fullscreen quad buffer");
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
  if (!current_command_buffer_ || !source || !destination) {
    return;
  }

  // Use blit encoder for simple copy if formats match and no scaling needed
  if (source->pixelFormat() == destination->pixelFormat() &&
      source->width() == destination->width() &&
      source->height() == destination->height()) {
    
    MTL::BlitCommandEncoder* blit_encoder = current_command_buffer_->blitCommandEncoder();
    if (blit_encoder) {
      blit_encoder->setLabel(NS::String::string("Present Blit", NS::UTF8StringEncoding));
      
      // Copy entire texture
      blit_encoder->copyFromTexture(
          source, 0, 0,  // sourceSlice, sourceLevel
          MTL::Origin(0, 0, 0),  // sourceOrigin
          MTL::Size(source->width(), source->height(), 1),  // sourceSize
          destination, 0, 0,  // destSlice, destLevel  
          MTL::Origin(0, 0, 0)  // destOrigin
      );
      
      blit_encoder->endEncoding();
      blit_encoder->release();
      return;
    }
  }

  // Otherwise, use render encoder with blit pipeline for format conversion/scaling
  MTL::RenderPassDescriptor* render_pass = MTL::RenderPassDescriptor::alloc()->init();
  if (!render_pass) {
    return;
  }

  // Configure render pass to render to destination texture
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
      // Set pipeline and draw fullscreen quad
      render_encoder->setRenderPipelineState(blit_pipeline_);
      render_encoder->setVertexBuffer(fullscreen_quad_buffer_, 0, 0);
      render_encoder->setFragmentTexture(source, 0);
      
      // Draw triangle strip (4 vertices for quad)
      render_encoder->drawPrimitives(MTL::PrimitiveTypeTriangleStrip, NS::UInteger(0), NS::UInteger(4));
    }
    
    render_encoder->endEncoding();
    render_encoder->release();
  }
  
  render_pass->release();
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
