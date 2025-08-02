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

// Objective-C imports for Metal rendering
#import <Metal/Metal.h>
#import <simd/simd.h>

// Vertex structure matching ImmediateVertex
struct MetalImmediateVertex {
  simd::float2 position;
  simd::float2 texcoord;
  uint32_t color;
};

// Metal shaders as strings
static const char* kMetalShaderSource = R"(
#include <metal_stdlib>
#include <simd/simd.h>
using namespace metal;

struct Vertex {
  float2 position [[attribute(0)]];
  float2 texcoord [[attribute(1)]];
  uint color [[attribute(2)]];
};

struct VertexOut {
  float4 position [[position]];
  float2 texcoord;
  float4 color;
};

struct Uniforms {
  float4x4 projection_matrix;
};

vertex VertexOut vertex_main(Vertex in [[stage_in]],
                           constant Uniforms& uniforms [[buffer(1)]]) {
  VertexOut out;
  out.position = uniforms.projection_matrix * float4(in.position, 0.0, 1.0);
  out.texcoord = in.texcoord;
  
  // Convert packed RGBA color to float4
  out.color = float4(
    float((in.color >> 0) & 0xFF) / 255.0,
    float((in.color >> 8) & 0xFF) / 255.0,
    float((in.color >> 16) & 0xFF) / 255.0,
    float((in.color >> 24) & 0xFF) / 255.0
  );
  
  return out;
}

fragment float4 fragment_main(VertexOut in [[stage_in]],
                            texture2d<float> tex [[texture(0)]],
                            sampler samp [[sampler(0)]]) {
  float4 tex_color = tex.sample(samp, in.texcoord);
  return in.color * tex_color;
}

fragment float4 fragment_main_no_texture(VertexOut in [[stage_in]]) {
  return in.color;
}
)";

namespace xe {
namespace ui {
namespace metal {

MetalImmediateDrawer::MetalImmediateDrawer(MetalProvider* provider)
    : provider_(provider) {
  device_ = provider_->GetDevice();
}

MetalImmediateDrawer::~MetalImmediateDrawer() = default;

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

bool MetalImmediateDrawer::Initialize() {
  // Create Metal library from shader source
  NSError* error = nil;
  NSString* shader_source = [NSString stringWithUTF8String:kMetalShaderSource];
  
  id<MTLDevice> mtl_device = (__bridge id<MTLDevice>)device_;
  id<MTLLibrary> library = [mtl_device newLibraryWithSource:shader_source options:nil error:&error];
  
  if (!library) {
    XELOGE("Metal immediate drawer failed to create shader library: {}", 
           error ? [[error localizedDescription] UTF8String] : "unknown error");
    return false;
  }
  
  id<MTLFunction> vertex_function = [library newFunctionWithName:@"vertex_main"];
  id<MTLFunction> fragment_function = [library newFunctionWithName:@"fragment_main"];
  id<MTLFunction> fragment_function_no_texture = [library newFunctionWithName:@"fragment_main_no_texture"];
  
  if (!vertex_function || !fragment_function || !fragment_function_no_texture) {
    XELOGE("Metal immediate drawer failed to load shader functions");
    return false;
  }
  
  // Create vertex descriptor
  MTLVertexDescriptor* vertex_descriptor = [MTLVertexDescriptor vertexDescriptor];
  
  // Position attribute (float2)
  vertex_descriptor.attributes[0].format = MTLVertexFormatFloat2;
  vertex_descriptor.attributes[0].offset = 0;
  vertex_descriptor.attributes[0].bufferIndex = 0;
  
  // Texcoord attribute (float2)
  vertex_descriptor.attributes[1].format = MTLVertexFormatFloat2;
  vertex_descriptor.attributes[1].offset = 8;
  vertex_descriptor.attributes[1].bufferIndex = 0;
  
  // Color attribute (uint32)
  vertex_descriptor.attributes[2].format = MTLVertexFormatUInt;
  vertex_descriptor.attributes[2].offset = 16;
  vertex_descriptor.attributes[2].bufferIndex = 0;
  
  // Layout
  vertex_descriptor.layouts[0].stride = 20; // sizeof(ImmediateVertex)
  vertex_descriptor.layouts[0].stepRate = 1;
  vertex_descriptor.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
  
  // Create render pipeline descriptor for textured triangles
  MTLRenderPipelineDescriptor* pipeline_desc = [[MTLRenderPipelineDescriptor alloc] init];
  pipeline_desc.label = @"ImGui Textured Pipeline";
  pipeline_desc.vertexFunction = vertex_function;
  pipeline_desc.fragmentFunction = fragment_function;
  pipeline_desc.vertexDescriptor = vertex_descriptor;
  
  // Configure color attachment
  pipeline_desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
  pipeline_desc.colorAttachments[0].blendingEnabled = YES;
  pipeline_desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
  pipeline_desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
  pipeline_desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
  pipeline_desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
  pipeline_desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
  pipeline_desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
  
  // Create textured pipeline
  pipeline_triangle_ = (__bridge MTL::RenderPipelineState*)[mtl_device newRenderPipelineStateWithDescriptor:pipeline_desc error:&error];
  if (!pipeline_triangle_) {
    XELOGE("Metal immediate drawer failed to create textured pipeline: {}", 
           error ? [[error localizedDescription] UTF8String] : "unknown error");
    return false;
  }
  
  // Create pipeline for non-textured triangles
  pipeline_desc.fragmentFunction = fragment_function_no_texture;
  pipeline_desc.label = @"ImGui Non-Textured Pipeline";
  
  pipeline_line_ = (__bridge MTL::RenderPipelineState*)[mtl_device newRenderPipelineStateWithDescriptor:pipeline_desc error:&error];
  if (!pipeline_line_) {
    XELOGE("Metal immediate drawer failed to create non-textured pipeline: {}", 
           error ? [[error localizedDescription] UTF8String] : "unknown error");
    return false;
  }
  
  XELOGI("Metal immediate drawer initialized with render pipelines");
  return true;
}

void MetalImmediateDrawer::Shutdown() {
  // TODO(wmarti): Cleanup Metal immediate drawing resources
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
  
  XELOGI("Metal CreateTexture created {}x{} texture successfully", width, height);
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
  
  // Create uniform buffer for projection matrix
  id<MTLDevice> mtl_device = (__bridge id<MTLDevice>)device_;
  id<MTLBuffer> uniform_buffer = [mtl_device newBufferWithBytes:&projection_matrix 
                                                         length:sizeof(projection_matrix) 
                                                        options:MTLResourceStorageModeShared];
  
  // Set the uniform buffer
  id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)current_render_encoder_;
  [encoder setVertexBuffer:uniform_buffer offset:0 atIndex:1];
  
  // XELOGI("Metal immediate drawer Begin: coordinate space {}x{}", 
  //        coordinate_space_width, coordinate_space_height);
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
  
  // Set vertex buffer
  id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)current_render_encoder_;
  [encoder setVertexBuffer:vertex_buffer offset:0 atIndex:0];
  
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
  
  // XELOGI("Metal immediate drawer BeginDrawBatch: {} vertices", batch.vertex_count);
}

void MetalImmediateDrawer::Draw(const ImmediateDraw& draw) {
  assert_true(batch_open_);
  
  id<MTLRenderCommandEncoder> encoder = (__bridge id<MTLRenderCommandEncoder>)current_render_encoder_;
  
  // Choose pipeline based on whether we have a texture
  // Note: pipeline_triangle_ = textured, pipeline_line_ = non-textured
  MTL::RenderPipelineState* pipeline = draw.texture ? pipeline_triangle_ : pipeline_line_;
  [encoder setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)pipeline];
  
  // Set texture and sampler if present
  if (draw.texture) {
    MetalImmediateTexture* metal_texture = static_cast<MetalImmediateTexture*>(draw.texture);
    [encoder setFragmentTexture:(__bridge id<MTLTexture>)metal_texture->texture atIndex:0];
    [encoder setFragmentSamplerState:(__bridge id<MTLSamplerState>)metal_texture->sampler atIndex:0];
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
    // Disable scissor testing by setting scissor rect to full render target
    // We need to get the actual render target dimensions
    // For now, use a very large scissor rect to effectively disable it
    MTLScissorRect full_rect;
    full_rect.x = 0;
    full_rect.y = 0;
    full_rect.width = 8192;  // Large enough for any reasonable display
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
  
  // Clean up batch resources
  current_index_buffer_ = nullptr;
  
  // XELOGI("Metal immediate drawer EndDrawBatch: batch completed");
}

void MetalImmediateDrawer::End() {
  // Clear Metal rendering state
  current_command_buffer_ = nullptr;
  current_render_encoder_ = nullptr;
  
  // XELOGI("Metal immediate drawer End: drawing completed");
}

}  // namespace metal
}  // namespace ui
}  // namespace xe
