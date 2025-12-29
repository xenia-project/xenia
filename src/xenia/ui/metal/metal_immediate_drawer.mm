/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/metal/metal_immediate_drawer.h"

#include "Metal/Metal.hpp"

#include "xenia/base/logging.h"
#include "xenia/ui/metal/metal_provider.h"
#include "xenia/ui/metal/metal_presenter.h"

// Generated shaders
#include "xenia/ui/shaders/bytecode/metal/immediate_ps.h"
#include "xenia/ui/shaders/bytecode/metal/immediate_vs.h"

// Objective-C imports for Metal rendering
#import <Metal/Metal.h>
#import <simd/simd.h>

namespace xe {
namespace ui {
namespace metal {

MetalImmediateDrawer::MetalImmediateDrawer(MetalProvider* provider)
    : provider_(provider) {
  device_ = provider_->GetDevice();
}

MetalImmediateDrawer::~MetalImmediateDrawer() {
  Shutdown();
}

MetalImmediateDrawer::MetalImmediateTexture::~MetalImmediateTexture() {
  if (texture) {
    texture->release();
    texture = nullptr;
  }
  if (sampler) {
    sampler->release();
    sampler = nullptr;
  }
}

static MTL::Function* CreateFunction(MTL::Device* device, const void* data,
                                     size_t size, const char* name) {
  dispatch_data_t d = dispatch_data_create(data, size, nullptr,
                                           DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  NS::Error* error = nullptr;
  MTL::Library* lib = device->newLibrary(d, &error);
  dispatch_release(d);
  if (!lib) {
    XELOGE("MetalImmediateDrawer: Failed to create library for {}: {}", name,
           error ? error->localizedDescription()->utf8String() : "unknown");
    return nullptr;
  }
  // XeSL MSL uses xesl_entry as the entry point
  NS::String* ns_name =
      NS::String::string("xesl_entry", NS::UTF8StringEncoding);
  MTL::Function* fn = lib->newFunction(ns_name);
  lib->release();
  if (!fn) {
    XELOGE("MetalImmediateDrawer: Failed to load function xesl_entry for {}",
           name);
  }
  return fn;
}

bool MetalImmediateDrawer::Initialize() {
  // Create 1x1 white texture for non-textured draws
  MTL::TextureDescriptor* white_desc = MTL::TextureDescriptor::alloc()->init();
  white_desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  white_desc->setWidth(1);
  white_desc->setHeight(1);
  white_desc->setUsage(MTL::TextureUsageShaderRead);
  white_desc->setStorageMode(MTL::StorageModeShared);
  
  white_texture_ = device_->newTexture(white_desc);
  white_desc->release();
  
  if (!white_texture_) {
    XELOGE("MetalImmediateDrawer: Failed to create white texture");
    return false;
  }
  
  // Fill with white (0xFFFFFFFF)
  uint32_t white_pixel = 0xFFFFFFFF;
  white_texture_->replaceRegion(MTL::Region(0, 0, 0, 1, 1, 1), 0, &white_pixel, 4);

  // Create default sampler
  MTL::SamplerDescriptor* sampler_desc = MTL::SamplerDescriptor::alloc()->init();
  sampler_desc->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sampler_desc->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sampler_desc->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sampler_desc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  default_sampler_ = device_->newSamplerState(sampler_desc);
  sampler_desc->release();

  // Load Shaders
  MTL::Function* vs = CreateFunction(device_, immediate_vs_metallib,
                                     sizeof(immediate_vs_metallib), "immediate_vs");
  if (!vs) return false;

  MTL::Function* ps = CreateFunction(device_, immediate_ps_metallib,
                                     sizeof(immediate_ps_metallib), "immediate_ps");
  if (!ps) {
    vs->release();
    return false;
  }

  // Create vertex descriptor
  MTL::VertexDescriptor* vertex_descriptor = MTL::VertexDescriptor::alloc()->init();
  
  // Position attribute (float2) -> xesl_entry_stageInput(xesl_float2, xe_in_position, 0, POSITION)
  // Vertex shader input:
  // xesl_entry_stageInput(xesl_float2, xe_in_position, 0, POSITION)
  // xesl_entry_stageInput(xesl_float2, xe_in_texcoord, 1, TEXCOORD)
  // xesl_entry_stageInput(xesl_uint, xe_in_color, 2, COLOR)
  
  // Attribute 0: Position
  vertex_descriptor->attributes()->object(0)->setFormat(MTL::VertexFormatFloat2);
  vertex_descriptor->attributes()->object(0)->setOffset(
      offsetof(ImmediateVertex, x));
  vertex_descriptor->attributes()->object(0)->setBufferIndex(1);
  
  // Attribute 1: Texcoord
  vertex_descriptor->attributes()->object(1)->setFormat(MTL::VertexFormatFloat2);
  vertex_descriptor->attributes()->object(1)->setOffset(
      offsetof(ImmediateVertex, u));
  vertex_descriptor->attributes()->object(1)->setBufferIndex(1);
  
  // Attribute 2: Color
  vertex_descriptor->attributes()->object(2)->setFormat(MTL::VertexFormatUChar4Normalized);
  vertex_descriptor->attributes()->object(2)->setOffset(
      offsetof(ImmediateVertex, color));
  vertex_descriptor->attributes()->object(2)->setBufferIndex(1);
  
  // Layout (Buffer 1)
  vertex_descriptor->layouts()->object(1)->setStride(sizeof(ImmediateVertex));
  vertex_descriptor->layouts()->object(1)->setStepRate(1);
  vertex_descriptor->layouts()->object(1)->setStepFunction(MTL::VertexStepFunctionPerVertex);
  
  // Create render pipeline descriptor
  MTL::RenderPipelineDescriptor* pipeline_desc = MTL::RenderPipelineDescriptor::alloc()->init();
  pipeline_desc->setLabel(NS::String::string("ImGui Pipeline", NS::UTF8StringEncoding));
  pipeline_desc->setVertexFunction(vs);
  pipeline_desc->setFragmentFunction(ps);
  pipeline_desc->setVertexDescriptor(vertex_descriptor);
  
  // Configure color attachment
  auto* color_attachment = pipeline_desc->colorAttachments()->object(0);
  // Match the CAMetalLayer format used by the Metal presenter.
  color_attachment->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
  color_attachment->setBlendingEnabled(true);
  color_attachment->setRgbBlendOperation(MTL::BlendOperationAdd);
  color_attachment->setAlphaBlendOperation(MTL::BlendOperationAdd);
  color_attachment->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
  color_attachment->setSourceAlphaBlendFactor(MTL::BlendFactorSourceAlpha);
  color_attachment->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
  color_attachment->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
  
  NS::Error* error = nullptr;
  pipeline_textured_ = device_->newRenderPipelineState(pipeline_desc, &error);
  
  vertex_descriptor->release();
  pipeline_desc->release();
  vs->release();
  ps->release();
  
  if (!pipeline_textured_) {
    XELOGE("MetalImmediateDrawer: Failed to create pipeline: {}", 
           error ? error->localizedDescription()->utf8String() : "unknown");
    return false;
  }
  
  XELOGI("MetalImmediateDrawer initialized");
  return true;
}

void MetalImmediateDrawer::Shutdown() {
  if (white_texture_) {
    white_texture_->release();
    white_texture_ = nullptr;
  }
  if (default_sampler_) {
    default_sampler_->release();
    default_sampler_ = nullptr;
  }
  if (pipeline_textured_) {
    pipeline_textured_->release();
    pipeline_textured_ = nullptr;
  }
  XELOGI("Metal immediate drawer shut down");
}

std::unique_ptr<ImmediateTexture> MetalImmediateDrawer::CreateTexture(
    uint32_t width, uint32_t height, ImmediateTextureFilter filter,
    bool repeat, const uint8_t* data) {
  assert_not_null(data);
  
  // Create Metal texture descriptor
  MTL::TextureDescriptor* texture_desc = MTL::TextureDescriptor::alloc()->init();
  texture_desc->setTextureType(MTL::TextureType2D);
  texture_desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
  texture_desc->setWidth(width);
  texture_desc->setHeight(height);
  texture_desc->setDepth(1);
  texture_desc->setMipmapLevelCount(1);
  texture_desc->setArrayLength(1);
  texture_desc->setSampleCount(1);
  texture_desc->setUsage(MTL::TextureUsageShaderRead);
  texture_desc->setStorageMode(MTL::StorageModeManaged);
  
  // Create Metal texture
  MTL::Texture* metal_texture = device_->newTexture(texture_desc);
  texture_desc->release();
  
  if (!metal_texture) {
    XELOGE("Metal CreateTexture failed to create {}x{} texture", width, height);
    return nullptr;
  }
  
  // Upload texture data
  MTL::Region region(0, 0, 0, width, height, 1);
  uint32_t bytes_per_row = width * 4; // RGBA8 = 4 bytes per pixel
  metal_texture->replaceRegion(region, 0, data, bytes_per_row);
  
  // Create sampler state
  MTL::SamplerDescriptor* sampler_desc = MTL::SamplerDescriptor::alloc()->init();
  if (filter == ImmediateTextureFilter::kLinear) {
    sampler_desc->setMinFilter(MTL::SamplerMinMagFilterLinear);
    sampler_desc->setMagFilter(MTL::SamplerMinMagFilterLinear);
  } else {
    sampler_desc->setMinFilter(MTL::SamplerMinMagFilterNearest);
    sampler_desc->setMagFilter(MTL::SamplerMinMagFilterNearest);
  }
  
  if (repeat) {
    sampler_desc->setSAddressMode(MTL::SamplerAddressModeRepeat);
    sampler_desc->setTAddressMode(MTL::SamplerAddressModeRepeat);
  } else {
    sampler_desc->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
    sampler_desc->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  }
  
  MTL::SamplerState* sampler_state = device_->newSamplerState(sampler_desc);
  sampler_desc->release();
  
  if (!sampler_state) {
    XELOGE("Metal CreateTexture failed to create sampler state");
    metal_texture->release();
    return nullptr;
  }
  
  // Create our texture wrapper
  auto texture = std::make_unique<MetalImmediateTexture>(width, height);
  texture->texture = metal_texture;
  texture->sampler = sampler_state;
  
  // XELOGI("Metal CreateTexture created {}x{} texture successfully", width, height);
  return std::move(texture);
}

std::unique_ptr<ImmediateTexture>
MetalImmediateDrawer::CreateTextureFromMetal(MTL::Texture* texture,
                                             MTL::SamplerState* sampler) {
  if (!texture) {
    return nullptr;
  }
  auto wrapped = std::make_unique<MetalImmediateTexture>(
      static_cast<uint32_t>(texture->width()),
      static_cast<uint32_t>(texture->height()));
  wrapped->texture = texture;
  wrapped->sampler = sampler ? sampler : default_sampler_;
  if (wrapped->texture) {
    wrapped->texture->retain();
  }
  if (wrapped->sampler) {
    wrapped->sampler->retain();
  }
  return wrapped;
}

void MetalImmediateDrawer::Begin(UIDrawContext& ui_draw_context,
                                 float coordinate_space_width,
                                 float coordinate_space_height) {
  ImmediateDrawer::Begin(ui_draw_context, coordinate_space_width, coordinate_space_height);
  
  // Cast to Metal UI draw context to get Metal command objects
  const MetalUIDrawContext& metal_ui_draw_context =
      static_cast<const MetalUIDrawContext&>(ui_draw_context);
  
  current_command_buffer_ = (__bridge MTL::CommandBuffer*)metal_ui_draw_context.command_buffer();
  current_render_encoder_ = (__bridge MTL::RenderCommandEncoder*)metal_ui_draw_context.render_encoder();
  
  // Set up coordinate space size inverse for UI coordinate space
  // ImGui expects (0,0) at top-left, (width,height) at bottom-right
  simd::float2 coordinate_space_size_inv = {
    1.0f / coordinate_space_width,
    1.0f / coordinate_space_height,
  };
  
  // immediate.vs.xesl uses push constants in buffer(0)
  // xesl_float2 xe_coordinate_space_size_inv;
  
  id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)current_render_encoder_;
  // Ensure UI draws use a full-screen viewport/scissor regardless of prior GPU state.
  MTLViewport viewport;
  viewport.originX = 0.0;
  viewport.originY = 0.0;
  viewport.width = metal_ui_draw_context.render_target_width();
  viewport.height = metal_ui_draw_context.render_target_height();
  viewport.znear = 0.0;
  viewport.zfar = 1.0;
  [encoder setViewport:viewport];

  MTLScissorRect scissor_rect;
  scissor_rect.x = 0;
  scissor_rect.y = 0;
  scissor_rect.width = metal_ui_draw_context.render_target_width();
  scissor_rect.height = metal_ui_draw_context.render_target_height();
  [encoder setScissorRect:scissor_rect];
  [encoder setVertexBytes:&coordinate_space_size_inv length:sizeof(coordinate_space_size_inv) atIndex:0];
}

void MetalImmediateDrawer::BeginDrawBatch(const ImmediateDrawBatch& batch) {
  assert_false(batch_open_);
  batch_open_ = true;
  
  // Create vertex buffer for this batch
  id<MTLDevice> mtl_device = (__bridge id<MTLDevice>)device_;
  size_t vertex_buffer_size = batch.vertex_count * sizeof(ImmediateVertex);
  
  id<MTLBuffer> vertex_buffer = [mtl_device newBufferWithBytes:batch.vertices 
                                                        length:vertex_buffer_size 
                                                       options:MTLResourceStorageModeShared];
  
  if (!vertex_buffer) {
    XELOGE("Metal immediate drawer failed to create vertex buffer for {} vertices", batch.vertex_count);
    return;
  }
  
  // Set vertex buffer (index 1 to avoid conflict with push constants at 0, if using buffers?)
  // xesl bindings map:
  // immediate.vs.xesl: push constants at buffer(0)
  // attributes: usually mapped to buffer(1) if not explicit?
  // Let's check generated immediate_vs.h or air.
  // XeSL usually maps attributes to [[attribute(n)]].
  // Render pipeline descriptor defines attribute layout.
  // bufferIndex 0 in descriptor means [[buffer(0)]] for vertex data?
  // BUT push constants are also buffer(0)?
  // XeSL MSL backend: push constants are buffer(0).
  // Vertex buffers start at buffer(1) usually (kVertexBufferBindPoint = 1).
  // I need to ensure my VertexDescriptor uses bufferIndex 1 for attributes.
  
  id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)current_render_encoder_;
  // Use index 1 for vertex buffer, since 0 is push constants
  [encoder setVertexBuffer:vertex_buffer offset:0 atIndex:1];
  
  // Create index buffer if indices are provided
  if (batch.indices && batch.index_count > 0) {
    size_t index_buffer_size = batch.index_count * sizeof(uint16_t);
    id<MTLBuffer> index_buffer = [mtl_device newBufferWithBytes:batch.indices 
                                                         length:index_buffer_size 
                                                        options:MTLResourceStorageModeShared];
    current_index_buffer_ = (__bridge void*)index_buffer;
  } else {
    current_index_buffer_ = nullptr;
  }
}

void MetalImmediateDrawer::Draw(const ImmediateDraw& draw) {
  assert_true(batch_open_);
  
  id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)current_render_encoder_;
  
  // Use the textured pipeline (it handles both cases via white texture fallback)
  [encoder setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)pipeline_textured_];
  uint32_t scissor_left, scissor_top, scissor_width, scissor_height;
  if (!ScissorToRenderTarget(draw, scissor_left, scissor_top, scissor_width,
                             scissor_height)) {
    return;
  }
  MTLScissorRect scissor_rect;
  scissor_rect.x = scissor_left;
  scissor_rect.y = scissor_top;
  scissor_rect.width = scissor_width;
  scissor_rect.height = scissor_height;
  [encoder setScissorRect:scissor_rect];
  
  // Set texture and sampler
  MTL::Texture* texture_to_bind = white_texture_;
  MTL::SamplerState* sampler_to_bind = default_sampler_;
  
  if (draw.texture) {
    MetalImmediateTexture* metal_texture = static_cast<MetalImmediateTexture*>(draw.texture);
    texture_to_bind = metal_texture->texture;
    sampler_to_bind = metal_texture->sampler;
  }
  
  // immediate.ps.xesl bindings:
  // sampler: set=0, binding=0 -> texture(0), sampler(0)
  [encoder setFragmentTexture:(__bridge id<MTLTexture>)texture_to_bind atIndex:0];
  if (sampler_to_bind) {
    [encoder setFragmentSamplerState:(__bridge id<MTLSamplerState>)sampler_to_bind atIndex:0];
  }
  
  // Convert primitive type
  MTLPrimitiveType primitive_type = (draw.primitive_type == ImmediatePrimitiveType::kTriangles) 
      ? MTLPrimitiveTypeTriangle : MTLPrimitiveTypeLine;
  
  // Draw indexed or non-indexed primitives
  if (current_index_buffer_) {
    id<MTLBuffer> index_buffer = (__bridge id<MTLBuffer>)current_index_buffer_;
    [encoder drawIndexedPrimitives:primitive_type
                        indexCount:draw.count
                         indexType:MTLIndexTypeUInt16
                       indexBuffer:index_buffer
                 indexBufferOffset:draw.index_offset * sizeof(uint16_t)
                     instanceCount:1
                        baseVertex:draw.base_vertex
                      baseInstance:0];
  } else {
    [encoder drawPrimitives:primitive_type
                vertexStart:draw.base_vertex
                vertexCount:draw.count];
  }
}

void MetalImmediateDrawer::EndDrawBatch() {
  assert_true(batch_open_);
  batch_open_ = false;
  current_index_buffer_ = nullptr;
}

void MetalImmediateDrawer::End() {
  current_command_buffer_ = nullptr;
  current_render_encoder_ = nullptr;
}

}  // namespace metal
}  // namespace ui
}  // namespace xe
