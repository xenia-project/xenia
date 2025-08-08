/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/metal/metal_render_target_cache.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/gpu/xenos.h"

namespace xe {
namespace gpu {
namespace metal {

// MetalRenderTarget implementation
MetalRenderTargetCache::MetalRenderTarget::~MetalRenderTarget() {
  if (texture_) {
    texture_->release();
    texture_ = nullptr;
  }
  if (msaa_texture_) {
    msaa_texture_->release();
    msaa_texture_ = nullptr;
  }
}

// MetalRenderTargetCache implementation
MetalRenderTargetCache::MetalRenderTargetCache(
    const RegisterFile& register_file,
    const Memory& memory,
    TraceWriter* trace_writer,
    uint32_t draw_resolution_scale_x,
    uint32_t draw_resolution_scale_y,
    MetalCommandProcessor& command_processor)
    : RenderTargetCache(register_file, memory, trace_writer,
                       draw_resolution_scale_x, draw_resolution_scale_y),
      command_processor_(command_processor),
      trace_writer_(trace_writer) {
}

MetalRenderTargetCache::~MetalRenderTargetCache() {
  Shutdown(true);
}

bool MetalRenderTargetCache::Initialize() {
  device_ = command_processor_.GetMetalDevice();
  if (!device_) {
    XELOGE("MetalRenderTargetCache: No Metal device available");
    return false;
  }
  
  // Create EDRAM buffer (10MB)
  constexpr size_t kEdramSizeBytes = 10 * 1024 * 1024;
  edram_buffer_ = device_->newBuffer(kEdramSizeBytes, MTL::ResourceStorageModePrivate);
  if (!edram_buffer_) {
    XELOGE("MetalRenderTargetCache: Failed to create EDRAM buffer");
    return false;
  }
  edram_buffer_->setLabel(NS::String::string("EDRAM Buffer", NS::UTF8StringEncoding));
  
  // Initialize base class
  InitializeCommon();
  
  XELOGI("MetalRenderTargetCache: Initialized with {}x{} resolution scale",
         draw_resolution_scale_x(), draw_resolution_scale_y());
  
  return true;
}

void MetalRenderTargetCache::Shutdown(bool from_destructor) {
  if (!from_destructor) {
    ClearCache();
  }
  
  // Clean up dummy target
  dummy_color_target_.reset();
  
  if (cached_render_pass_descriptor_) {
    cached_render_pass_descriptor_->release();
    cached_render_pass_descriptor_ = nullptr;
  }
  
  if (edram_buffer_) {
    edram_buffer_->release();
    edram_buffer_ = nullptr;
  }
  
  // Destroy all render targets
  DestroyAllRenderTargets(!from_destructor);
  
  // Shutdown base class
  if (!from_destructor) {
    ShutdownCommon();
  }
}

void MetalRenderTargetCache::ClearCache() {
  // Clear current bindings
  for (uint32_t i = 0; i < 4; ++i) {
    current_color_targets_[i] = nullptr;
  }
  current_depth_target_ = nullptr;
  render_pass_descriptor_dirty_ = true;
  
  // Call base implementation
  RenderTargetCache::ClearCache();
}

void MetalRenderTargetCache::BeginFrame() {
  // Reset dummy target clear flag
  dummy_color_target_needs_clear_ = true;
  
  // Call base implementation
  RenderTargetCache::BeginFrame();
}

bool MetalRenderTargetCache::Update(bool is_rasterization_done,
                                    reg::RB_DEPTHCONTROL normalized_depth_control,
                                    uint32_t normalized_color_mask,
                                    const Shader& vertex_shader) {
  XELOGI("MetalRenderTargetCache::Update called - is_rasterization_done={}",
         is_rasterization_done);
  
  // Call base implementation - this does all the heavy lifting
  if (!RenderTargetCache::Update(is_rasterization_done,
                                 normalized_depth_control,
                                 normalized_color_mask,
                                 vertex_shader)) {
    XELOGE("MetalRenderTargetCache::Update - Base class Update failed");
    return false;
  }
  
  // After base class update, retrieve the actual render targets that were selected
  // This is the KEY to connecting base class management with Metal-specific rendering
  RenderTarget* const* accumulated_targets = last_update_accumulated_render_targets();
  
  XELOGI("MetalRenderTargetCache::Update - Got accumulated targets from base class");
  
  // Debug: Check what we actually got
  for (uint32_t i = 0; i < 5; ++i) {
    if (accumulated_targets[i]) {
      XELOGI("  accumulated_targets[{}] = {:p} (key 0x{:08X})", 
             i, (void*)accumulated_targets[i], accumulated_targets[i]->key().key);
    } else {
      XELOGI("  accumulated_targets[{}] = nullptr", i);
    }
  }
  
  // Clear current targets first
  for (uint32_t i = 0; i < 4; ++i) {
    current_color_targets_[i] = nullptr;
  }
  current_depth_target_ = nullptr;
  
  // Extract the depth target (index 0)
  if (accumulated_targets[0]) {
    current_depth_target_ = static_cast<MetalRenderTarget*>(accumulated_targets[0]);
    XELOGD("MetalRenderTargetCache::Update - Depth target bound: key={:08X}",
           current_depth_target_->key().key);
  }
  
  // Extract color targets (indices 1-4)
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    if (accumulated_targets[i + 1]) {
      current_color_targets_[i] = static_cast<MetalRenderTarget*>(accumulated_targets[i + 1]);
      XELOGD("MetalRenderTargetCache::Update - Color target {} bound: key={:08X}",
             i, current_color_targets_[i]->key().key);
    }
  }
  
  // TODO: Handle ownership transfers if needed (see D3D12/Vulkan PerformTransfersAndResolveClears)
  // For now, we'll skip this as we're just trying to get basic rendering working
  
  // Mark render pass descriptor as dirty so it gets recreated with the new targets
  render_pass_descriptor_dirty_ = true;
  
  return true;
}

uint32_t MetalRenderTargetCache::GetMaxRenderTargetWidth() const {
  // Metal maximum texture dimension
  return 16384;
}

uint32_t MetalRenderTargetCache::GetMaxRenderTargetHeight() const {
  // Metal maximum texture dimension
  return 16384;
}

RenderTargetCache::RenderTarget* MetalRenderTargetCache::CreateRenderTarget(
    RenderTargetKey key) {
  XELOGI("MetalRenderTargetCache::CreateRenderTarget called - key={:08X}, is_depth={}",
         key.key, key.is_depth);
  
  // Calculate dimensions
  uint32_t width = key.GetWidth();
  uint32_t height = 0;  // Will be determined by viewport
  
  // Apply resolution scaling
  width *= draw_resolution_scale_x();
  
  // Create Metal render target
  auto* render_target = new MetalRenderTarget(key);
  
  // Create the texture based on format
  MTL::Texture* texture = nullptr;
  uint32_t samples = 1 << uint32_t(key.msaa_samples);
  
  if (key.is_depth) {
    texture = CreateDepthTexture(width, height, key.GetDepthFormat(), samples);
  } else {
    texture = CreateColorTexture(width, height, key.GetColorFormat(), samples);
  }
  
  if (!texture) {
    delete render_target;
    return nullptr;
  }
  
  render_target->SetTexture(texture);
  
  // Store in our map for later retrieval
  render_target_map_[key.key] = render_target;
  
  XELOGI("MetalRenderTargetCache: Created render target - {}x{}, {} samples, key 0x{:08X}",
         width, height, samples, key.key);
  
  return render_target;
}

bool MetalRenderTargetCache::IsHostDepthEncodingDifferent(
    xenos::DepthRenderTargetFormat format) const {
  // Metal uses different depth encoding than Xbox 360
  // D24S8 on Xbox 360 vs D32Float_S8 on Metal
  return format == xenos::DepthRenderTargetFormat::kD24S8 ||
         format == xenos::DepthRenderTargetFormat::kD24FS8;
}

MTL::Texture* MetalRenderTargetCache::CreateColorTexture(
    uint32_t width, uint32_t height,
    xenos::ColorRenderTargetFormat format,
    uint32_t samples) {
  
  MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
  desc->setWidth(width);
  desc->setHeight(height ? height : 720);  // Default height if not specified
  desc->setPixelFormat(GetColorPixelFormat(format));
  desc->setTextureType(samples > 1 ? MTL::TextureType2DMultisample : MTL::TextureType2D);
  desc->setSampleCount(samples);
  desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  desc->setStorageMode(MTL::StorageModePrivate);
  
  MTL::Texture* texture = device_->newTexture(desc);
  desc->release();
  
  return texture;
}

MTL::Texture* MetalRenderTargetCache::CreateDepthTexture(
    uint32_t width, uint32_t height,
    xenos::DepthRenderTargetFormat format,
    uint32_t samples) {
  
  MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
  desc->setWidth(width);
  desc->setHeight(height ? height : 720);  // Default height if not specified
  desc->setPixelFormat(GetDepthPixelFormat(format));
  desc->setTextureType(samples > 1 ? MTL::TextureType2DMultisample : MTL::TextureType2D);
  desc->setSampleCount(samples);
  desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  desc->setStorageMode(MTL::StorageModePrivate);
  
  MTL::Texture* texture = device_->newTexture(desc);
  desc->release();
  
  return texture;
}

MTL::PixelFormat MetalRenderTargetCache::GetColorPixelFormat(
    xenos::ColorRenderTargetFormat format) const {
  switch (format) {
    case xenos::ColorRenderTargetFormat::k_8_8_8_8:
    case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return MTL::PixelFormatBGRA8Unorm;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_AS_10_10_10_10:
      return MTL::PixelFormatRGB10A2Unorm;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT_AS_16_16_16_16:
      return MTL::PixelFormatRGB10A2Unorm;  // Approximation
    case xenos::ColorRenderTargetFormat::k_16_16:
      return MTL::PixelFormatRG16Snorm;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16:
      return MTL::PixelFormatRGBA16Snorm;
    case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
      return MTL::PixelFormatRG16Float;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      return MTL::PixelFormatRGBA16Float;
    case xenos::ColorRenderTargetFormat::k_32_FLOAT:
      return MTL::PixelFormatR32Float;
    case xenos::ColorRenderTargetFormat::k_32_32_FLOAT:
      return MTL::PixelFormatRG32Float;
    default:
      XELOGE("MetalRenderTargetCache: Unsupported color format {}", 
             static_cast<uint32_t>(format));
      return MTL::PixelFormatBGRA8Unorm;
  }
}

MTL::PixelFormat MetalRenderTargetCache::GetDepthPixelFormat(
    xenos::DepthRenderTargetFormat format) const {
  switch (format) {
    case xenos::DepthRenderTargetFormat::kD24S8:
    case xenos::DepthRenderTargetFormat::kD24FS8:
      // Metal doesn't have D24S8, use D32Float_S8
      return MTL::PixelFormatDepth32Float_Stencil8;
    default:
      XELOGE("MetalRenderTargetCache: Unsupported depth format {}",
             static_cast<uint32_t>(format));
      return MTL::PixelFormatDepth32Float_Stencil8;
  }
}

MTL::RenderPassDescriptor* MetalRenderTargetCache::GetRenderPassDescriptor(
    uint32_t expected_sample_count) {
  
  if (!render_pass_descriptor_dirty_ && cached_render_pass_descriptor_) {
    return cached_render_pass_descriptor_;
  }
  
  // Release old descriptor
  if (cached_render_pass_descriptor_) {
    cached_render_pass_descriptor_->release();
    cached_render_pass_descriptor_ = nullptr;
  }
  
  // Create new descriptor
  cached_render_pass_descriptor_ = MTL::RenderPassDescriptor::renderPassDescriptor();
  if (!cached_render_pass_descriptor_) {
    XELOGE("MetalRenderTargetCache: Failed to create render pass descriptor");
    return nullptr;
  }
  cached_render_pass_descriptor_->retain();
  
  bool has_any_render_target = false;
  
  // Bind the actual render targets retrieved from base class in Update()
  
  // Bind depth target if present
  if (current_depth_target_ && current_depth_target_->texture()) {
    auto* depth_attachment = cached_render_pass_descriptor_->depthAttachment();
    depth_attachment->setTexture(current_depth_target_->texture());
    depth_attachment->setLoadAction(MTL::LoadActionLoad);
    depth_attachment->setStoreAction(MTL::StoreActionStore);
    depth_attachment->setClearDepth(1.0);
    
    // Check if this format has stencil
    xenos::DepthRenderTargetFormat depth_format = 
        current_depth_target_->key().GetDepthFormat();
    if (depth_format == xenos::DepthRenderTargetFormat::kD24S8) {
      auto* stencil_attachment = cached_render_pass_descriptor_->stencilAttachment();
      stencil_attachment->setTexture(current_depth_target_->texture());
      stencil_attachment->setLoadAction(MTL::LoadActionLoad);
      stencil_attachment->setStoreAction(MTL::StoreActionStore);
      stencil_attachment->setClearStencil(0);
    }
    
    has_any_render_target = true;
    
    // Track this as a real render target for capture
    last_real_depth_target_ = current_depth_target_;
    
    XELOGI("MetalRenderTargetCache: Bound depth target to render pass (REAL target)");
  }
  
  // Bind color targets
  for (uint32_t i = 0; i < 4; ++i) {
    if (current_color_targets_[i] && current_color_targets_[i]->texture()) {
      auto* color_attachment = cached_render_pass_descriptor_->colorAttachments()->object(i);
      color_attachment->setTexture(current_color_targets_[i]->texture());
      color_attachment->setLoadAction(MTL::LoadActionLoad);
      color_attachment->setStoreAction(MTL::StoreActionStore);
      color_attachment->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
      
      has_any_render_target = true;
      
      // Track this as a real render target for capture
      last_real_color_targets_[i] = current_color_targets_[i];
      
      XELOGI("MetalRenderTargetCache: Bound color target {} to render pass (REAL target, {}x{}, key 0x{:08X})", 
             i, current_color_targets_[i]->texture()->width(), 
             current_color_targets_[i]->texture()->height(),
             current_color_targets_[i]->key().key);
    }
  }
  
  // If no render targets are bound, create a dummy target
  // This can happen during trace dumps or when no valid render targets are configured
  if (!has_any_render_target) {
    XELOGW("MetalRenderTargetCache: No render targets bound, creating dummy target");
    
    if (!dummy_color_target_) {
      dummy_color_target_ = std::make_unique<MetalRenderTarget>(RenderTargetKey());
      MTL::Texture* dummy_texture = CreateColorTexture(
          1280, 720, xenos::ColorRenderTargetFormat::k_8_8_8_8, 1);
      if (dummy_texture) {
        dummy_color_target_->SetTexture(dummy_texture);
      }
    }
    
    if (dummy_color_target_ && dummy_color_target_->texture()) {
      auto* color_attachment = cached_render_pass_descriptor_->colorAttachments()->object(0);
      color_attachment->setTexture(dummy_color_target_->texture());
      color_attachment->setLoadAction(dummy_color_target_needs_clear_ ? 
                                     MTL::LoadActionClear : MTL::LoadActionLoad);
      color_attachment->setStoreAction(MTL::StoreActionStore);
      
      if (dummy_color_target_needs_clear_) {
        color_attachment->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
        dummy_color_target_needs_clear_ = false;
      }
    }
  }
  
  render_pass_descriptor_dirty_ = false;
  
  return cached_render_pass_descriptor_;
}

MTL::Texture* MetalRenderTargetCache::GetColorTarget(uint32_t index) const {
  if (index >= 4 || !current_color_targets_[index]) {
    return nullptr;
  }
  return current_color_targets_[index]->texture();
}

MTL::Texture* MetalRenderTargetCache::GetDepthTarget() const {
  if (!current_depth_target_) {
    return nullptr;
  }
  return current_depth_target_->texture();
}

MTL::Texture* MetalRenderTargetCache::GetDummyColorTarget() const {
  if (dummy_color_target_ && dummy_color_target_->texture()) {
    return dummy_color_target_->texture();
  }
  return nullptr;
}

MTL::Texture* MetalRenderTargetCache::GetLastRealColorTarget(uint32_t index) const {
  if (index >= 4 || !last_real_color_targets_[index]) {
    return nullptr;
  }
  return last_real_color_targets_[index]->texture();
}

MTL::Texture* MetalRenderTargetCache::GetLastRealDepthTarget() const {
  if (!last_real_depth_target_) {
    return nullptr;
  }
  return last_real_depth_target_->texture();
}

void MetalRenderTargetCache::RestoreEdram(const void* snapshot) {
  if (!edram_buffer_ || !snapshot) {
    XELOGE("MetalRenderTargetCache::RestoreEdram: No EDRAM buffer or snapshot");
    return;
  }
  
  // Copy the snapshot to EDRAM buffer
  constexpr size_t kEdramSizeBytes = 10 * 1024 * 1024;  // 10MB EDRAM
  
  // Need to use a staging buffer for CPU->GPU copy
  MTL::Buffer* staging = device_->newBuffer(snapshot, kEdramSizeBytes,
                                            MTL::ResourceStorageModeShared);
  if (!staging) {
    XELOGE("MetalRenderTargetCache::RestoreEdram: Failed to create staging buffer");
    return;
  }
  
  // Copy from staging to EDRAM buffer
  MTL::CommandQueue* queue = command_processor_.GetMetalCommandQueue();
  if (queue) {
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    if (cmd) {
      MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
      if (blit) {
        blit->copyFromBuffer(staging, 0, edram_buffer_, 0, kEdramSizeBytes);
        blit->endEncoding();
      }
      cmd->commit();
      cmd->waitUntilCompleted();
    }
  }
  
  staging->release();
  
  XELOGI("MetalRenderTargetCache::RestoreEdram: Restored {}MB EDRAM snapshot",
         kEdramSizeBytes / (1024 * 1024));
}

bool MetalRenderTargetCache::Resolve(Memory& memory, uint32_t& written_address,
                                     uint32_t& written_length) {
  // TODO: Implement resolve
  written_address = 0;
  written_length = 0;
  return false;
}

void MetalRenderTargetCache::LoadTiledData(MTL::CommandBuffer* command_buffer,
                                           MTL::Texture* texture,
                                           uint32_t edram_base,
                                           uint32_t pitch_tiles,
                                           uint32_t height_tiles,
                                           bool is_depth) {
  // TODO: Implement tile loading from EDRAM
}

void MetalRenderTargetCache::StoreTiledData(MTL::CommandBuffer* command_buffer,
                                            MTL::Texture* texture,
                                            uint32_t edram_base,
                                            uint32_t pitch_tiles,
                                            uint32_t height_tiles,
                                            bool is_depth) {
  // TODO: Implement tile storing to EDRAM
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe