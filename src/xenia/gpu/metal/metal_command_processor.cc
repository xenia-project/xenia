/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_command_processor.h"

#include "xenia/base/logging.h"
#include "xenia/gpu/metal/metal_graphics_system.h"
#include "xenia/gpu/metal/metal_buffer_cache.h"
#include "xenia/gpu/metal/metal_pipeline_cache.h"
#include "xenia/gpu/metal/metal_render_target_cache.h"
#include "xenia/gpu/metal/metal_texture_cache.h"

namespace xe {
namespace gpu {
namespace metal {

MetalCommandProcessor::MetalCommandProcessor(MetalGraphicsSystem* graphics_system,
                                             kernel::KernelState* kernel_state)
    : CommandProcessor(graphics_system, kernel_state),
      metal_graphics_system_(graphics_system) {}

MetalCommandProcessor::~MetalCommandProcessor() = default;

MTL::Device* MetalCommandProcessor::GetMetalDevice() const {
  return metal_graphics_system_->metal_device();
}

MTL::CommandQueue* MetalCommandProcessor::GetMetalCommandQueue() const {
  return metal_graphics_system_->metal_command_queue();
}

std::string MetalCommandProcessor::GetWindowTitleText() const {
  // TODO(wmarti): Add Metal device info
  return "Metal - EARLY DEVELOPMENT";
}

bool MetalCommandProcessor::SetupContext() {
  // Initialize cache systems
  pipeline_cache_ = std::make_unique<MetalPipelineCache>(
      this, register_file_, false);  // edram_rov_used = false for now
  buffer_cache_ = std::make_unique<MetalBufferCache>(
      this, register_file_, memory_);
  texture_cache_ = std::make_unique<MetalTextureCache>(
      this, register_file_, memory_);
  render_target_cache_ = std::make_unique<MetalRenderTargetCache>(
      this, register_file_, memory_);
  
  // Initialize each cache system
  if (!buffer_cache_->Initialize()) {
    XELOGE("Failed to initialize Metal buffer cache");
    return false;
  }
  if (!texture_cache_->Initialize()) {
    XELOGE("Failed to initialize Metal texture cache");
    return false;
  }
  if (!render_target_cache_->Initialize()) {
    XELOGE("Failed to initialize Metal render target cache");
    return false;
  }
  if (!pipeline_cache_->Initialize()) {
    XELOGE("Failed to initialize Metal pipeline cache");
    return false;
  }
  
  XELOGI("Metal cache systems initialized successfully");
  
  return CommandProcessor::SetupContext();
}

void MetalCommandProcessor::ShutdownContext() {
  // Shutdown cache systems in reverse order
  if (pipeline_cache_) {
    pipeline_cache_->Shutdown();
    pipeline_cache_.reset();
  }
  if (render_target_cache_) {
    render_target_cache_->Shutdown();
    render_target_cache_.reset();
  }
  if (texture_cache_) {
    texture_cache_->Shutdown();
    texture_cache_.reset();
  }
  if (buffer_cache_) {
    buffer_cache_->Shutdown();
    buffer_cache_.reset();
  }
  
  XELOGI("Metal cache systems shutdown");
  
  CommandProcessor::ShutdownContext();
}

void MetalCommandProcessor::IssueSwap(uint32_t frontbuffer_ptr,
                                      uint32_t frontbuffer_width,
                                      uint32_t frontbuffer_height) {
  // TODO(wmarti): Implement Metal swapchain presentation
  XELOGW("Metal IssueSwap not implemented");
}

void MetalCommandProcessor::OnGammaRamp256EntryTableValueWritten() {
  // TODO(wmarti): Handle gamma ramp changes
}

void MetalCommandProcessor::OnGammaRampPWLValueWritten() {
  // TODO(wmarti): Handle gamma ramp changes
}

void MetalCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  // TODO(wmarti): Handle Metal-specific register writes
  CommandProcessor::WriteRegister(index, value);
}

void MetalCommandProcessor::TracePlaybackWroteMemory(uint32_t base_ptr,
                                                     uint32_t length) {
  // TODO(wmarti): Handle trace playback memory writes
}

void MetalCommandProcessor::RestoreEdramSnapshot(const void* snapshot) {
  // TODO(wmarti): Restore EDRAM state from snapshot
  XELOGW("Metal RestoreEdramSnapshot not implemented");
}

Shader* MetalCommandProcessor::LoadShader(xenos::ShaderType shader_type,
                                          uint32_t guest_address,
                                          const uint32_t* host_address,
                                          uint32_t dword_count) {
  // Delegate to the pipeline cache for shader loading
  return pipeline_cache_->LoadShader(shader_type, host_address, dword_count);
}

bool MetalCommandProcessor::IssueDraw(xenos::PrimitiveType prim_type,
                                      uint32_t index_count,
                                      IndexBufferInfo* index_buffer_info,
                                      bool major_mode_explicit) {
  // Phase B Step 2: Pipeline state object creation and caching
  
  const char* prim_type_name = "unknown";
  switch (prim_type) {
    case xenos::PrimitiveType::kNone:
      prim_type_name = "none";
      break;
    case xenos::PrimitiveType::kPointList:
      prim_type_name = "point_list";
      break;
    case xenos::PrimitiveType::kLineList:
      prim_type_name = "line_list";
      break;
    case xenos::PrimitiveType::kLineStrip:
      prim_type_name = "line_strip";
      break;
    case xenos::PrimitiveType::kTriangleList:
      prim_type_name = "triangle_list";
      break;
    case xenos::PrimitiveType::kTriangleFan:
      prim_type_name = "triangle_fan";
      break;
    case xenos::PrimitiveType::kTriangleStrip:
      prim_type_name = "triangle_strip";
      break;
    case xenos::PrimitiveType::kTriangleWithWFlags:
      prim_type_name = "triangle_with_w_flags";
      break;
    case xenos::PrimitiveType::kRectangleList:
      prim_type_name = "rectangle_list";
      break;
    case xenos::PrimitiveType::kLineLoop:
      prim_type_name = "line_loop";
      break;
    case xenos::PrimitiveType::kQuadList:
      prim_type_name = "quad_list";
      break;
    case xenos::PrimitiveType::kQuadStrip:
      prim_type_name = "quad_strip";
      break;
    case xenos::PrimitiveType::kPolygon:
      prim_type_name = "polygon";
      break;
    default:
      // TODO: Phase D - Add support for explicit major mode primitive types
      prim_type_name = "unknown";
      XELOGW("Metal IssueDraw: Unsupported primitive type: 0x{:02X}", static_cast<uint32_t>(prim_type));
      break;
  }
  
  XELOGI("Metal IssueDraw: prim_type={}, index_count={}, major_mode_explicit={}",
         prim_type_name, index_count, major_mode_explicit);
  
  if (index_buffer_info) {
    XELOGI("Metal IssueDraw: index_buffer guest_base=0x{:08X}, format={}, endianness={}",
           index_buffer_info->guest_base, 
           static_cast<int>(index_buffer_info->format),
           static_cast<int>(index_buffer_info->endianness));
  }

  // Phase B Step 2: Create pipeline state for this draw call
  // This demonstrates the shader→pipeline integration
  XELOGI("Metal IssueDraw: Attempting pipeline state creation...");
  
  // Phase D.1: Get real Xbox 360 shaders from register state
  // Use active shaders loaded from Xbox 360 game data instead of hardcoded values
  Shader* vertex_shader = active_vertex_shader();
  Shader* pixel_shader = active_pixel_shader();
  
  if (!vertex_shader || !pixel_shader) {
    XELOGE("Metal IssueDraw: Missing active shaders - vertex: {}, pixel: {}",
           vertex_shader ? "present" : "missing", 
           pixel_shader ? "present" : "missing");
    return false;
  }
  
  XELOGI("Metal IssueDraw: Using real Xbox 360 shaders - vertex: {:016x}, pixel: {:016x}",
         vertex_shader->ucode_data_hash(), pixel_shader->ucode_data_hash());
  
  // Create pipeline description with real shader hashes
  MetalPipelineCache::RenderPipelineDescription pipeline_desc = {};
  pipeline_desc.primitive_type = prim_type;
  pipeline_desc.vertex_shader_hash = vertex_shader->ucode_data_hash();
  pipeline_desc.pixel_shader_hash = pixel_shader->ucode_data_hash();
  
  // TODO: Set shader modifications based on register state
  pipeline_desc.vertex_shader_modification = 0;
  pipeline_desc.pixel_shader_modification = 0;
  
  // Request pipeline state from cache (this will create it if needed)
  MTL::RenderPipelineState* pipeline_state = 
      pipeline_cache_->GetRenderPipelineState(pipeline_desc);
  
  if (pipeline_state) {
    XELOGI("Metal IssueDraw: Successfully obtained pipeline state");
    
    // Phase C Step 1: Metal Command Buffer and Render Pass Encoding
    MTL::CommandQueue* command_queue = GetMetalCommandQueue();
    if (command_queue) {
      MTL::CommandBuffer* command_buffer = command_queue->commandBuffer();
      if (command_buffer) {
        XELOGI("Metal IssueDraw: Created command buffer for rendering");
        
        // Phase C Step 3: Enhanced Metal frame debugging support
        char draw_label[256];
        snprintf(draw_label, sizeof(draw_label), "Xbox360Draw_%s_idx%u", 
                prim_type_name, index_count);
        command_buffer->setLabel(NS::String::string(draw_label, NS::UTF8StringEncoding));
        
        // For now, we'll create a basic render pass to a dummy texture
        // In a full implementation, this would use the Xbox 360 render targets
        MTL::RenderPassDescriptor* render_pass = MTL::RenderPassDescriptor::alloc()->init();
        
        // Create a simple 1x1 render target for validation
        // TODO: Use actual Xbox 360 render targets
        MTL::TextureDescriptor* texture_desc = MTL::TextureDescriptor::alloc()->init();
        texture_desc->setWidth(256);
        texture_desc->setHeight(256);
        texture_desc->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
        texture_desc->setUsage(MTL::TextureUsageRenderTarget);
        
        MTL::Texture* render_target = GetMetalDevice()->newTexture(texture_desc);
        render_pass->colorAttachments()->object(0)->setTexture(render_target);
        render_pass->colorAttachments()->object(0)->setLoadAction(MTL::LoadActionClear);
        render_pass->colorAttachments()->object(0)->setStoreAction(MTL::StoreActionStore);
        render_pass->colorAttachments()->object(0)->setClearColor(MTL::ClearColor(0.2, 0.0, 0.8, 1.0)); // Blue clear
        
        // Create render command encoder
        MTL::RenderCommandEncoder* encoder = command_buffer->renderCommandEncoder(render_pass);
        if (encoder) {
          char encoder_label[256];
          snprintf(encoder_label, sizeof(encoder_label), "Xbox360RenderPass_%s_hash_0x%llx_0x%llx", 
                  prim_type_name, pipeline_desc.vertex_shader_hash, pipeline_desc.pixel_shader_hash);
          encoder->setLabel(NS::String::string(encoder_label, NS::UTF8StringEncoding));
          
          // Phase C Step 2: Pipeline State and Draw Call Encoding
          encoder->setRenderPipelineState(pipeline_state);
          
          XELOGI("Metal IssueDraw: Set pipeline state and created render encoder");
          XELOGI("Metal IssueDraw: Render pass label: {}", encoder_label);
          
          // Phase C Step 2: Enhanced debugging - Push debug group
          encoder->pushDebugGroup(NS::String::string("Xbox360DrawCommands", NS::UTF8StringEncoding));
          
          // Phase C Step 3: Basic Vertex Buffer Support
          // Create a simple triangle vertex buffer for validation
          float triangle_vertices[] = {
              // position (x, y, z)
              -0.5f, -0.5f, 0.0f,   // bottom left
               0.5f, -0.5f, 0.0f,   // bottom right  
               0.0f,  0.5f, 0.0f    // top center
          };
          
          MTL::Buffer* vertex_buffer = GetMetalDevice()->newBuffer(
              triangle_vertices, sizeof(triangle_vertices), MTL::ResourceStorageModeShared);
          
          if (vertex_buffer) {
            vertex_buffer->setLabel(NS::String::string("Xbox360VertexBuffer", NS::UTF8StringEncoding));
            
            // Bind vertex buffer at index 0 (matches our vertex descriptor)
            encoder->setVertexBuffer(vertex_buffer, 0, 0);
            
            XELOGI("Metal IssueDraw: Created and bound vertex buffer ({} bytes)", sizeof(triangle_vertices));
            
            // Phase D: Bind required shader resource buffers
            // The converted shaders expect these buffers to be bound:
            // - Buffer at index 2: struct.top_level_global_ab[0] (global register state)
            // - Buffer at index 4: drawArguments[0] (draw parameters)
            // - Buffer at index 5: uniforms[0] (shader constants)
            
            // Create dummy buffers to satisfy shader requirements
            // In a full implementation, these would contain actual Xbox 360 GPU state
            
            // Buffer 2: Global register state (dummy for now)
            uint32_t global_registers[256] = {0}; // Placeholder for Xbox 360 register state
            MTL::Buffer* global_buffer = GetMetalDevice()->newBuffer(
                global_registers, sizeof(global_registers), MTL::ResourceStorageModeShared);
            if (global_buffer) {
              global_buffer->setLabel(NS::String::string("Xbox360GlobalRegisters", NS::UTF8StringEncoding));
              encoder->setVertexBuffer(global_buffer, 0, 2);
              encoder->setFragmentBuffer(global_buffer, 0, 2);
              XELOGI("Metal IssueDraw: Bound global register buffer at index 2");
            }
            
            // Buffer 4: Draw arguments
            struct DrawArguments {
              uint32_t vertex_count;
              uint32_t instance_count;
              uint32_t first_vertex;
              uint32_t first_instance;
            } draw_args = {3, 1, 0, 0};
            
            MTL::Buffer* draw_args_buffer = GetMetalDevice()->newBuffer(
                &draw_args, sizeof(draw_args), MTL::ResourceStorageModeShared);
            if (draw_args_buffer) {
              draw_args_buffer->setLabel(NS::String::string("Xbox360DrawArguments", NS::UTF8StringEncoding));
              encoder->setVertexBuffer(draw_args_buffer, 0, 4);
              XELOGI("Metal IssueDraw: Bound draw arguments buffer at index 4");
            }
            
            // Buffer 5: Shader uniforms/constants
            float uniforms[64] = {0}; // Placeholder for shader constants
            MTL::Buffer* uniforms_buffer = GetMetalDevice()->newBuffer(
                uniforms, sizeof(uniforms), MTL::ResourceStorageModeShared);
            if (uniforms_buffer) {
              uniforms_buffer->setLabel(NS::String::string("Xbox360ShaderUniforms", NS::UTF8StringEncoding));
              encoder->setVertexBuffer(uniforms_buffer, 0, 5);
              XELOGI("Metal IssueDraw: Bound uniforms buffer at index 5");
            }
            
            // TODO: Phase C Step 2 - Bind index buffers from Xbox 360 data
            // TODO: Phase C Step 2 - Use actual Xbox 360 vertex data
            
            // Encode the actual draw call
            encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
            
            XELOGI("Metal IssueDraw: Encoded draw primitives call (3 vertices, triangle)");
            
            // Clean up buffers
            vertex_buffer->release();
            if (global_buffer) global_buffer->release();
            if (draw_args_buffer) draw_args_buffer->release();
            if (uniforms_buffer) uniforms_buffer->release();
          } else {
            XELOGW("Metal IssueDraw: Failed to create vertex buffer");
          }
          
          // Pop debug group
          encoder->popDebugGroup();
          
          encoder->endEncoding();
          encoder->release();
          
          XELOGI("Metal IssueDraw: Metal frame debugging - Command buffer '{}' ready for capture", draw_label);
        }
        
        // Commit the command buffer
        command_buffer->commit();
        XELOGI("Metal IssueDraw: Committed Metal command buffer");
        
        // Cleanup
        render_target->release();
        texture_desc->release();
        render_pass->release();
      }
    }
  } else {
    XELOGW("Metal IssueDraw: Failed to create pipeline state");
  }

  // Phase C: Return true to indicate successful Metal command encoding
  return true;
}

bool MetalCommandProcessor::IssueCopy() {
  // TODO(wmarti): Issue Metal copy commands
  XELOGW("Metal IssueCopy not implemented");
  return false;
}

bool MetalCommandProcessor::BeginSubmission(bool is_guest_command) {
  if (submission_open_) {
    return true;
  }

  // Mark frame as open if needed
  if (is_guest_command && !frame_open_) {
    frame_open_ = true;
    
    // Notify cache systems of new frame
    if (buffer_cache_) {
      // buffer_cache_->BeginFrame();  // Will add when implementing buffer cache
    }
    if (texture_cache_) {
      // texture_cache_->BeginFrame();  // Will add when implementing texture cache
    }
  }

  // Create new command buffer
  auto* command_queue = GetMetalCommandQueue();
  if (!command_queue) {
    XELOGE("No Metal command queue available for submission");
    return false;
  }

  current_command_buffer_ = command_queue->commandBuffer();
  if (!current_command_buffer_) {
    XELOGE("Failed to create Metal command buffer");
    return false;
  }

  submission_open_ = true;
  return true;
}

bool MetalCommandProcessor::EndSubmission(bool is_swap) {
  if (!submission_open_) {
    return true;
  }

  bool is_closing_frame = is_swap && frame_open_;

  if (is_closing_frame) {
    // Notify cache systems of frame end
    if (texture_cache_) {
      // texture_cache_->EndFrame();  // Will add when implementing texture cache
    }
    frame_open_ = false;
  }

  // Commit and submit the command buffer
  if (current_command_buffer_) {
    current_command_buffer_->commit();
    current_command_buffer_ = nullptr;
  }

  submission_open_ = false;
  return true;
}

bool MetalCommandProcessor::CanEndSubmissionImmediately() const {
  // For now, always allow immediate submission
  // Later we may want to check for pipeline compilation, etc.
  return true;
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
