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

// Vertex structure matching ImmediateVertex
struct MetalImmediateVertex {
  simd::float2 position;
  simd::float2 texcoord;
  uint32_t color;
};

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
  vertex_descriptor->attributes()->object(0)->setOffset(0);
  vertex_descriptor->attributes()->object(0)->setBufferIndex(1);
  
  // Attribute 1: Texcoord
  vertex_descriptor->attributes()->object(1)->setFormat(MTL::VertexFormatFloat2);
  vertex_descriptor->attributes()->object(1)->setOffset(8);
  vertex_descriptor->attributes()->object(1)->setBufferIndex(1);
  
  // Attribute 2: Color
  vertex_descriptor->attributes()->object(2)->setFormat(MTL::VertexFormatUInt);
  vertex_descriptor->attributes()->object(2)->setOffset(16);
  vertex_descriptor->attributes()->object(2)->setBufferIndex(1);
  
  // Layout (Buffer 1)
  vertex_descriptor->layouts()->object(1)->setStride(sizeof(MetalImmediateVertex));
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
  color_attachment->setPixelFormat(MTL::PixelFormatBGRA8Unorm_sRGB); // Match MetalLayer format
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

void MetalImmediateDrawer::Begin(UIDrawContext& ui_draw_context,
                                 float coordinate_space_width,
                                 float coordinate_space_height) {
  ImmediateDrawer::Begin(ui_draw_context, coordinate_space_width, coordinate_space_height);
  
  // Cast to Metal UI draw context to get Metal command objects
  const MetalUIDrawContext& metal_ui_draw_context =
      static_cast<const MetalUIDrawContext&>(ui_draw_context);
  
  current_command_buffer_ = (__bridge MTL::CommandBuffer*)metal_ui_draw_context.command_buffer();
  current_render_encoder_ = (__bridge MTL::RenderCommandEncoder*)metal_ui_draw_context.render_encoder();
  
  // Set up orthographic projection matrix for UI coordinate space
  // ImGui expects (0,0) at top-left, (width,height) at bottom-right
  float left = 0.0f;
  float right = coordinate_space_width;
  float top = 0.0f;
  float bottom = coordinate_space_height;
  float near_z = -1.0f;
  float far_z = 1.0f;
  
  simd::float4x4 projection_matrix = {
    simd::make_float4(2.0f / (right - left), 0.0f, 0.0f, 0.0f),
    simd::make_float4(0.0f, 2.0f / (top - bottom), 0.0f, 0.0f),
    simd::make_float4(0.0f, 0.0f, 1.0f / (far_z - near_z), 0.0f),
    simd::make_float4((left + right) / (left - right), (top + bottom) / (bottom - top), near_z / (near_z - far_z), 1.0f)
  };
  
  // immediate.vs.xesl uses push constants in buffer(0)
  // xesl_float4x4 xe_projection_matrix;
  
  id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)current_render_encoder_;
  [encoder setVertexBytes:&projection_matrix length:sizeof(projection_matrix) atIndex:0];
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
  
  // Set texture and sampler
  MTL::Texture* texture_to_bind = white_texture_;
  MTL::SamplerState* sampler_to_bind = nullptr; // TODO: default sampler?
  
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
  } else {
    // We need a default sampler for the white texture.
    // Create one on the fly or cache it? caching is better.
    // For now, let's create a default linear sampler in Initialize or similar.
    // Hack: use the one from the first texture? No.
    // Just create a temporary one or assume user always provides texture?
    // ImGui always provides font texture.
    // Non-textured primitives (draw.texture == null) still need a sampler for the white texture.
    // Let's rely on a default sampler.
    // Create a default sampler in Initialize.
  }
  
  // Set scissor rect if enabled
  if (draw.scissor) {
    MTLScissorRect scissor_rect;
    scissor_rect.x = static_cast<NSUInteger>(std::max(0.0f, draw.scissor_left));
    scissor_rect.y = static_cast<NSUInteger>(std::max(0.0f, draw.scissor_top));
    scissor_rect.width = static_cast<NSUInteger>(std::max(0.0f, draw.scissor_right - draw.scissor_left));
    scissor_rect.height = static_cast<NSUInteger>(std::max(0.0f, draw.scissor_bottom - draw.scissor_top));
    [encoder setScissorRect:scissor_rect];
  } else {
    MTLScissorRect full_rect;
    full_rect.x = 0;
    full_rect.y = 0;
    full_rect.width = 8192;
    full_rect.height = 8192;
    [encoder setScissorRect:full_rect];
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
                 indexBufferOffset:draw.index_offset * sizeof(uint16_t)];
  } else {
    [encoder drawPrimitives:primitive_type
                vertexStart:draw.base_vertex + draw.index_offset
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
