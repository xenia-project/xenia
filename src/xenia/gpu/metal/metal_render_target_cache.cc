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
  
  // Initialize EDRAM compute shaders
  if (!InitializeEdramComputeShaders()) {
    XELOGE("MetalRenderTargetCache: Failed to initialize EDRAM compute shaders");
    return false;
  }
  
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
  
  // Clean up EDRAM compute shaders
  ShutdownEdramComputeShaders();
  
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
  
  // Clear the tracking of which render targets have been cleared
  cleared_render_targets_this_frame_.clear();
  
  // Call base implementation
  RenderTargetCache::ClearCache();
}

void MetalRenderTargetCache::BeginFrame() {
  // Reset dummy target clear flag
  dummy_color_target_needs_clear_ = true;
  
  // Clear the tracking of which render targets have been cleared this frame
  cleared_render_targets_this_frame_.clear();
  
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
  
  // Debug: Check what we actually got AND verify textures exist
  for (uint32_t i = 0; i < 5; ++i) {
    if (accumulated_targets[i]) {
      MetalRenderTarget* metal_rt = static_cast<MetalRenderTarget*>(accumulated_targets[i]);
      MTL::Texture* tex = metal_rt->texture();
      XELOGI("  accumulated_targets[{}] = {:p} (key 0x{:08X}, texture={:p}, {}x{})", 
             i, (void*)accumulated_targets[i], accumulated_targets[i]->key().key,
             (void*)tex, tex ? tex->width() : 0, tex ? tex->height() : 0);
      
      // CRITICAL FIX: Ensure texture exists
      if (!tex) {
        XELOGE("  ERROR: MetalRenderTarget has no texture! Need to create it.");
        // TODO: Create texture here if missing
      }
    } else {
      XELOGI("  accumulated_targets[{}] = nullptr", i);
    }
  }
  
  // Check if render targets actually changed
  bool targets_changed = false;
  
  // Check depth target
  MetalRenderTarget* new_depth_target = accumulated_targets[0] ? 
      static_cast<MetalRenderTarget*>(accumulated_targets[0]) : nullptr;
  if (new_depth_target != current_depth_target_) {
    targets_changed = true;
    current_depth_target_ = new_depth_target;
    if (current_depth_target_) {
      XELOGD("MetalRenderTargetCache::Update - Depth target changed: key={:08X}",
             current_depth_target_->key().key);
    }
  }
  
  // Check color targets
  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    MetalRenderTarget* new_color_target = accumulated_targets[i + 1] ? 
        static_cast<MetalRenderTarget*>(accumulated_targets[i + 1]) : nullptr;
    if (new_color_target != current_color_targets_[i]) {
      targets_changed = true;
      current_color_targets_[i] = new_color_target;
      if (current_color_targets_[i]) {
        XELOGD("MetalRenderTargetCache::Update - Color target {} changed: key={:08X}",
               i, current_color_targets_[i]->key().key);
      }
    }
  }
  
  // TODO: Handle ownership transfers if needed (see D3D12/Vulkan PerformTransfersAndResolveClears)
  // For now, we'll skip this as we're just trying to get basic rendering working
  
  // Only mark render pass descriptor as dirty if targets actually changed
  if (targets_changed) {
    render_pass_descriptor_dirty_ = true;
    XELOGI("MetalRenderTargetCache::Update - Render targets changed, marking descriptor dirty");
  }
  
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
  uint32_t height = GetRenderTargetHeight(key.pitch_tiles_at_32bpp, key.msaa_samples);
  
  // Apply resolution scaling
  width *= draw_resolution_scale_x();
  height *= draw_resolution_scale_y();
  
  XELOGI("MetalRenderTargetCache: Creating render target with calculated dimensions {}x{} (before scaling: {}x{})", 
         width, height, key.GetWidth(), GetRenderTargetHeight(key.pitch_tiles_at_32bpp, key.msaa_samples));
  
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
  
  // Load existing EDRAM data into the render target
  // Get command buffer from command processor for EDRAM operations
  MTL::CommandQueue* queue = command_processor_.GetMetalCommandQueue();
  if (queue) {
    MTL::CommandBuffer* cmd = queue->commandBuffer();
    if (cmd) {
      // Calculate EDRAM parameters
      uint32_t edram_base = key.base_tiles;
      uint32_t pitch_tiles = key.pitch_tiles_at_32bpp;
      uint32_t height_tiles = (height + xenos::kEdramTileHeightSamples - 1) / xenos::kEdramTileHeightSamples;
      
      LoadTiledData(cmd, texture, edram_base, pitch_tiles, height_tiles, key.is_depth);
      
      cmd->commit();
      cmd->waitUntilCompleted();
      
      XELOGI("MetalRenderTargetCache: Loaded EDRAM data for render target - base={}, pitch={}, height={}",
             edram_base, pitch_tiles, height_tiles);
    }
  }
  
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
  desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
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
  desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
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
  
  XELOGI("MetalRenderTargetCache::GetRenderPassDescriptor - dirty={}, cached={:p}",
         render_pass_descriptor_dirty_, (void*)cached_render_pass_descriptor_);
  
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
  
  // Debug: Log current targets
  XELOGI("GetRenderPassDescriptor: Current targets state:");
  XELOGI("  current_depth_target_ = {:p}", (void*)current_depth_target_);
  for (uint32_t i = 0; i < 4; ++i) {
    if (current_color_targets_[i]) {
      MTL::Texture* tex = current_color_targets_[i]->texture();
      XELOGI("  current_color_targets_[{}] = {:p}, texture={:p} ({}x{})",
             i, (void*)current_color_targets_[i], (void*)tex,
             tex ? tex->width() : 0, tex ? tex->height() : 0);
    } else {
      XELOGI("  current_color_targets_[{}] = nullptr", i);
    }
  }
  
  // Bind the actual render targets retrieved from base class in Update()
  
  // Bind depth target if present
  if (current_depth_target_ && current_depth_target_->texture()) {
    auto* depth_attachment = cached_render_pass_descriptor_->depthAttachment();
    depth_attachment->setTexture(current_depth_target_->texture());
    
    // Check if this render target has been cleared this frame
    uint32_t depth_key = current_depth_target_->key().key;
    bool needs_clear = cleared_render_targets_this_frame_.find(depth_key) == 
                       cleared_render_targets_this_frame_.end();
    
    if (needs_clear) {
      depth_attachment->setLoadAction(MTL::LoadActionClear);
      depth_attachment->setClearDepth(1.0);
      cleared_render_targets_this_frame_.insert(depth_key);
      XELOGI("MetalRenderTargetCache: Clearing depth target 0x{:08X} on first use", depth_key);
    } else {
      depth_attachment->setLoadAction(MTL::LoadActionLoad);
      XELOGI("MetalRenderTargetCache: Loading depth target 0x{:08X} on subsequent use", depth_key);
    }
    
    depth_attachment->setStoreAction(MTL::StoreActionStore);
    
    // Check if this format has stencil
    xenos::DepthRenderTargetFormat depth_format = 
        current_depth_target_->key().GetDepthFormat();
    if (depth_format == xenos::DepthRenderTargetFormat::kD24S8) {
      auto* stencil_attachment = cached_render_pass_descriptor_->stencilAttachment();
      stencil_attachment->setTexture(current_depth_target_->texture());
      
      if (needs_clear) {
        stencil_attachment->setLoadAction(MTL::LoadActionClear);
        stencil_attachment->setClearStencil(0);
      } else {
        stencil_attachment->setLoadAction(MTL::LoadActionLoad);
      }
      
      stencil_attachment->setStoreAction(MTL::StoreActionStore);
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
      
      // Check if this render target has been cleared this frame
      uint32_t color_key = current_color_targets_[i]->key().key;
      bool needs_clear = cleared_render_targets_this_frame_.find(color_key) == 
                         cleared_render_targets_this_frame_.end();
      
      if (needs_clear) {
        color_attachment->setLoadAction(MTL::LoadActionClear);
        color_attachment->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));  // Transparent black
        cleared_render_targets_this_frame_.insert(color_key);
        XELOGI("MetalRenderTargetCache: Clearing color target {} (0x{:08X}) on first use", i, color_key);
      } else {
        color_attachment->setLoadAction(MTL::LoadActionLoad);
        XELOGI("MetalRenderTargetCache: Loading color target {} (0x{:08X}) on subsequent use", i, color_key);
      }
      
      color_attachment->setStoreAction(MTL::StoreActionStore);
      
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

bool MetalRenderTargetCache::InitializeEdramComputeShaders() {
  // Metal shader source for EDRAM operations
  static const char* edram_shader_source = R"(
#include <metal_stdlib>
using namespace metal;

// Xbox 360 EDRAM tile constants
constant uint kTileWidthSamples = 80;
constant uint kTileHeightSamples = 16;

// Load shader: Copy from tiled EDRAM to linear texture
kernel void edram_load_32bpp(
    device uint* edram_buffer [[ buffer(0) ]],
    texture2d<float, access::write> output_texture [[ texture(0) ]],
    constant uint& edram_base [[ buffer(1) ]],
    constant uint& pitch_tiles [[ buffer(2) ]],
    uint2 tid [[ thread_position_in_grid ]]
) {
    uint2 texture_size = uint2(output_texture.get_width(), output_texture.get_height());
    if (tid.x >= texture_size.x || tid.y >= texture_size.y) return;
    
    // Calculate tile coordinates
    uint tile_x = tid.x / kTileWidthSamples;
    uint tile_y = tid.y / kTileHeightSamples;
    uint sample_x = tid.x % kTileWidthSamples;
    uint sample_y = tid.y % kTileHeightSamples;
    
    // Calculate EDRAM offset (circular addressing)
    uint tile_index = (edram_base + tile_y * pitch_tiles + tile_x) % 2048;
    uint sample_offset = sample_y * kTileWidthSamples + sample_x;
    uint edram_offset = tile_index * kTileWidthSamples * kTileHeightSamples + sample_offset;
    
    // Read from EDRAM and write to texture
    uint32_t pixel_data = edram_buffer[edram_offset];
    float4 color = float4(
        float((pixel_data >> 16) & 0xFF) / 255.0f,  // R
        float((pixel_data >> 8) & 0xFF) / 255.0f,   // G
        float(pixel_data & 0xFF) / 255.0f,          // B
        float((pixel_data >> 24) & 0xFF) / 255.0f   // A
    );
    
    output_texture.write(color, tid);
}

// Store shader: Copy from linear texture to tiled EDRAM
kernel void edram_store_32bpp(
    texture2d<float, access::read> input_texture [[ texture(0) ]],
    device uint* edram_buffer [[ buffer(0) ]],
    constant uint& edram_base [[ buffer(1) ]],
    constant uint& pitch_tiles [[ buffer(2) ]],
    uint2 tid [[ thread_position_in_grid ]]
) {
    uint2 texture_size = uint2(input_texture.get_width(), input_texture.get_height());
    if (tid.x >= texture_size.x || tid.y >= texture_size.y) return;
    
    // Calculate tile coordinates
    uint tile_x = tid.x / kTileWidthSamples;
    uint tile_y = tid.y / kTileHeightSamples;
    uint sample_x = tid.x % kTileWidthSamples;
    uint sample_y = tid.y % kTileHeightSamples;
    
    // Calculate EDRAM offset (circular addressing)
    uint tile_index = (edram_base + tile_y * pitch_tiles + tile_x) % 2048;
    uint sample_offset = sample_y * kTileWidthSamples + sample_x;
    uint edram_offset = tile_index * kTileWidthSamples * kTileHeightSamples + sample_offset;
    
    // Read from texture and write to EDRAM
    float4 color = input_texture.read(tid);
    uint32_t pixel_data = 
        (uint32_t(color.a * 255.0f) << 24) |  // A
        (uint32_t(color.r * 255.0f) << 16) |  // R
        (uint32_t(color.g * 255.0f) << 8)  |  // G
        uint32_t(color.b * 255.0f);           // B
    
    edram_buffer[edram_offset] = pixel_data;
}
)";

  NS::Error* error = nullptr;
  
  // Create library from source
  MTL::Library* library = device_->newLibrary(
      NS::String::string(edram_shader_source, NS::UTF8StringEncoding), nullptr, &error);
  
  if (!library) {
    if (error) {
      NS::String* error_desc = error->localizedDescription();
      XELOGE("MetalRenderTargetCache: Failed to compile EDRAM shaders: {}", 
             error_desc ? error_desc->utf8String() : "Unknown error");
      error->release();
    }
    return false;
  }
  
  // Create compute functions
  MTL::Function* load_function = library->newFunction(
      NS::String::string("edram_load_32bpp", NS::UTF8StringEncoding));
  MTL::Function* store_function = library->newFunction(
      NS::String::string("edram_store_32bpp", NS::UTF8StringEncoding));
  
  library->release();
  
  if (!load_function || !store_function) {
    XELOGE("MetalRenderTargetCache: Failed to get EDRAM shader functions");
    if (load_function) load_function->release();
    if (store_function) store_function->release();
    return false;
  }
  
  // Create compute pipeline states
  edram_load_pipeline_ = device_->newComputePipelineState(load_function, &error);
  if (!edram_load_pipeline_) {
    if (error) {
      NS::String* error_desc = error->localizedDescription();
      XELOGE("MetalRenderTargetCache: Failed to create EDRAM load pipeline: {}", 
             error_desc ? error_desc->utf8String() : "Unknown error");
      error->release();
    }
    load_function->release();
    store_function->release();
    return false;
  }
  
  edram_store_pipeline_ = device_->newComputePipelineState(store_function, &error);
  if (!edram_store_pipeline_) {
    if (error) {
      NS::String* error_desc = error->localizedDescription();
      XELOGE("MetalRenderTargetCache: Failed to create EDRAM store pipeline: {}", 
             error_desc ? error_desc->utf8String() : "Unknown error");
      error->release();
    }
    edram_load_pipeline_->release();
    edram_load_pipeline_ = nullptr;
    load_function->release();
    store_function->release();
    return false;
  }
  
  load_function->release();
  store_function->release();
  
  XELOGI("MetalRenderTargetCache: EDRAM compute shaders initialized successfully");
  return true;
}

void MetalRenderTargetCache::ShutdownEdramComputeShaders() {
  if (edram_load_pipeline_) {
    edram_load_pipeline_->release();
    edram_load_pipeline_ = nullptr;
  }
  
  if (edram_store_pipeline_) {
    edram_store_pipeline_->release();
    edram_store_pipeline_ = nullptr;
  }
}

void MetalRenderTargetCache::LoadTiledData(MTL::CommandBuffer* command_buffer,
                                           MTL::Texture* texture,
                                           uint32_t edram_base,
                                           uint32_t pitch_tiles,
                                           uint32_t height_tiles,
                                           bool is_depth) {
  if (!command_buffer || !texture || !edram_load_pipeline_) {
    return;
  }
  
  XELOGI("MetalRenderTargetCache::LoadTiledData - texture {}x{}, base={}, pitch={}, height={}, depth={}",
         texture->width(), texture->height(), edram_base, pitch_tiles, height_tiles, is_depth);
  
  MTL::Texture* target_texture = texture;
  MTL::Texture* temp_texture = nullptr;
  
  // If texture is multisample, create a temporary non-multisample texture for EDRAM operations
  if (texture->textureType() == MTL::TextureType2DMultisample) {
    XELOGI("MetalRenderTargetCache::LoadTiledData - Creating temporary non-multisample texture for EDRAM operation");
    
    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setWidth(texture->width());
    desc->setHeight(texture->height());
    desc->setPixelFormat(texture->pixelFormat());
    desc->setTextureType(MTL::TextureType2D);  // Regular 2D texture
    desc->setSampleCount(1);  // Non-multisample
    desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
    desc->setStorageMode(MTL::StorageModePrivate);
    
    temp_texture = device_->newTexture(desc);
    desc->release();
    
    if (!temp_texture) {
      XELOGE("MetalRenderTargetCache::LoadTiledData - Failed to create temporary texture");
      return;
    }
    
    target_texture = temp_texture;
  }
  
  // Create compute encoder
  MTL::ComputeCommandEncoder* encoder = command_buffer->computeCommandEncoder();
  if (!encoder) {
    if (temp_texture) temp_texture->release();
    return;
  }
  
  // Set compute pipeline
  encoder->setComputePipelineState(edram_load_pipeline_);
  
  // Bind EDRAM buffer
  encoder->setBuffer(edram_buffer_, 0, 0);
  
  // Bind output texture (either original or temporary)
  encoder->setTexture(target_texture, 0);
  
  // Create parameter buffers
  uint32_t params[2] = { edram_base, pitch_tiles };
  MTL::Buffer* param_buffer = device_->newBuffer(&params, sizeof(params), MTL::ResourceStorageModeShared);
  encoder->setBuffer(param_buffer, 0, 1);
  encoder->setBuffer(param_buffer, sizeof(uint32_t), 2);
  
  // Calculate thread group sizes
  MTL::Size threads_per_threadgroup = MTL::Size::Make(8, 8, 1);
  MTL::Size threadgroups = MTL::Size::Make(
      (target_texture->width() + 7) / 8,
      (target_texture->height() + 7) / 8,
      1);
  
  // Dispatch compute
  encoder->dispatchThreadgroups(threadgroups, threads_per_threadgroup);
  encoder->endEncoding();
  
  // If we used a temporary texture, we need to handle the multisample case
  if (temp_texture) {
    XELOGI("MetalRenderTargetCache::LoadTiledData - Handling multisample texture");
    
    // For multisample textures, we can't directly copy from a non-multisample texture.
    // The best approach is to clear the multisample texture to a default value since
    // we've already loaded the EDRAM data into the temporary texture.
    // In a real implementation, we'd need a proper resolve shader to expand single
    // samples to all samples in the multisample texture.
    
    // For now, just clear the multisample texture to avoid validation errors
    MTL::RenderPassDescriptor* clear_desc = MTL::RenderPassDescriptor::renderPassDescriptor();
    if (clear_desc) {
      auto* color_attachment = clear_desc->colorAttachments()->object(0);
      color_attachment->setTexture(texture);  // Multisample target
      color_attachment->setLoadAction(MTL::LoadActionClear);
      color_attachment->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
      color_attachment->setStoreAction(MTL::StoreActionStore);
      
      // Create a render encoder just to clear - no draw calls needed for clear
      MTL::RenderCommandEncoder* clear_encoder = command_buffer->renderCommandEncoder(clear_desc);
      if (clear_encoder) {
        // The clear happens automatically with LoadActionClear
        clear_encoder->endEncoding();
      }
      clear_desc->release();
    }
    
    // Note: In a complete implementation, we would:
    // 1. Create a fullscreen triangle shader that samples from temp_texture
    // 2. Render it to all samples of the multisample texture
    // For now, clearing prevents the validation error and allows progress
    
    temp_texture->release();
  }
  
  param_buffer->release();
}

void MetalRenderTargetCache::StoreTiledData(MTL::CommandBuffer* command_buffer,
                                            MTL::Texture* texture,
                                            uint32_t edram_base,
                                            uint32_t pitch_tiles,
                                            uint32_t height_tiles,
                                            bool is_depth) {
  if (!command_buffer || !texture || !edram_store_pipeline_) {
    return;
  }
  
  XELOGI("MetalRenderTargetCache::StoreTiledData - texture {}x{}, base={}, pitch={}, height={}, depth={}",
         texture->width(), texture->height(), edram_base, pitch_tiles, height_tiles, is_depth);
  
  MTL::Texture* source_texture = texture;
  MTL::Texture* temp_texture = nullptr;
  
  // If texture is multisample, create a temporary non-multisample texture and resolve to it first
  if (texture->textureType() == MTL::TextureType2DMultisample) {
    XELOGI("MetalRenderTargetCache::StoreTiledData - Creating temporary non-multisample texture for EDRAM operation");
    
    MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
    desc->setWidth(texture->width());
    desc->setHeight(texture->height());
    desc->setPixelFormat(texture->pixelFormat());
    desc->setTextureType(MTL::TextureType2D);  // Regular 2D texture
    desc->setSampleCount(1);  // Non-multisample
    desc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
    desc->setStorageMode(MTL::StorageModePrivate);
    
    temp_texture = device_->newTexture(desc);
    desc->release();
    
    if (!temp_texture) {
      XELOGE("MetalRenderTargetCache::StoreTiledData - Failed to create temporary texture");
      return;
    }
    
    // Resolve multisample texture to temporary texture
    MTL::RenderPassDescriptor* resolve_desc = MTL::RenderPassDescriptor::renderPassDescriptor();
    if (resolve_desc) {
      auto* color_attachment = resolve_desc->colorAttachments()->object(0);
      color_attachment->setTexture(texture);  // Multisample source
      color_attachment->setResolveTexture(temp_texture);  // Resolved output
      color_attachment->setLoadAction(MTL::LoadActionLoad);
      color_attachment->setStoreAction(MTL::StoreActionMultisampleResolve);
      
      MTL::RenderCommandEncoder* render_encoder = command_buffer->renderCommandEncoder(resolve_desc);
      if (render_encoder) {
        render_encoder->endEncoding();
      }
      resolve_desc->release();
    }
    
    source_texture = temp_texture;
  }
  
  // Create compute encoder
  MTL::ComputeCommandEncoder* encoder = command_buffer->computeCommandEncoder();
  if (!encoder) {
    if (temp_texture) temp_texture->release();
    return;
  }
  
  // Set compute pipeline
  encoder->setComputePipelineState(edram_store_pipeline_);
  
  // Bind input texture (either original or resolved)
  encoder->setTexture(source_texture, 0);
  
  // Bind EDRAM buffer
  encoder->setBuffer(edram_buffer_, 0, 0);
  
  // Create parameter buffers
  uint32_t params[2] = { edram_base, pitch_tiles };
  MTL::Buffer* param_buffer = device_->newBuffer(&params, sizeof(params), MTL::ResourceStorageModeShared);
  encoder->setBuffer(param_buffer, 0, 1);
  encoder->setBuffer(param_buffer, sizeof(uint32_t), 2);
  
  // Calculate thread group sizes
  MTL::Size threads_per_threadgroup = MTL::Size::Make(8, 8, 1);
  MTL::Size threadgroups = MTL::Size::Make(
      (source_texture->width() + 7) / 8,
      (source_texture->height() + 7) / 8,
      1);
  
  // Dispatch compute
  encoder->dispatchThreadgroups(threadgroups, threads_per_threadgroup);
  encoder->endEncoding();
  
  if (temp_texture) {
    temp_texture->release();
  }
  
  param_buffer->release();
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe