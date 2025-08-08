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
#include "xenia/gpu/metal/metal_command_processor.h"
#include "xenia/gpu/xenos.h"


namespace xe {
namespace gpu {
namespace metal {

MetalRenderTargetCache::MetalRenderTargetCache(MetalCommandProcessor* command_processor,
                                               const RegisterFile* register_file,
                                               Memory* memory)
    : command_processor_(command_processor),
      register_file_(register_file),
      memory_(memory),
      edram_buffer_(nullptr),
      cached_render_pass_descriptor_(nullptr),
      render_pass_descriptor_dirty_(true),
      current_depth_target_(nullptr) {
  for (auto& rt : current_color_targets_) {
    rt = nullptr;
  }
}

MetalRenderTargetCache::~MetalRenderTargetCache() {
  Shutdown();
}

bool MetalRenderTargetCache::Initialize() {
  SCOPE_profile_cpu_f("gpu");

  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    XELOGE("Metal render target cache: Failed to get Metal device");
    return false;
  }

  // Create EDRAM buffer - 10MB of embedded DRAM used by Xbox 360 for render targets
  // Note: Could scale by draw_resolution_scale_x/y for higher resolution rendering
  uint32_t edram_size = xenos::kEdramSizeBytes;  // 10 MB
  
  edram_buffer_ = device->newBuffer(edram_size, MTL::ResourceStorageModePrivate);
  if (!edram_buffer_) {
    XELOGE("Metal render target cache: Failed to create EDRAM buffer ({}MB)", 
           edram_size / (1024 * 1024));
    device->release();
    return false;
  }
  edram_buffer_->setLabel(NS::String::string("Xbox360 EDRAM Buffer", NS::UTF8StringEncoding));
  
  XELOGI("Metal render target cache: Created {}MB EDRAM buffer", 
         edram_size / (1024 * 1024));
  
  device->release();

  XELOGD("Metal render target cache: Initialized successfully");
  
  return true;
}

void MetalRenderTargetCache::Shutdown() {
  SCOPE_profile_cpu_f("gpu");

  ClearCache();

  // Clean up dummy target first
  dummy_color_target_.reset();

  if (cached_render_pass_descriptor_) {
    cached_render_pass_descriptor_->release();
    cached_render_pass_descriptor_ = nullptr;
  }
  
  if (edram_buffer_) {
    edram_buffer_->release();
    edram_buffer_ = nullptr;
  }

  XELOGD("Metal render target cache: Shutdown complete");
}

void MetalRenderTargetCache::ClearCache() {
  SCOPE_profile_cpu_f("gpu");

  // Clear current render targets
  for (auto& rt : current_color_targets_) {
    rt = nullptr;
  }
  current_depth_target_ = nullptr;

  // Clear cache
  render_target_cache_.clear();
  
  render_pass_descriptor_dirty_ = true;

  XELOGD("Metal render target cache: Cache cleared");
}

bool MetalRenderTargetCache::SetRenderTargets(uint32_t rt_count, const uint32_t* color_targets,
                                              uint32_t depth_target) {
  SCOPE_profile_cpu_f("gpu");

  render_pass_descriptor_dirty_ = true;

  // Clear existing targets
  for (auto& rt : current_color_targets_) {
    rt = nullptr;
  }
  current_depth_target_ = nullptr;

  // Set up color targets
  for (uint32_t i = 0; i < rt_count && i < 4; ++i) {
    if (color_targets[i] == 0) {
      continue;
    }

    // Parse render target info from the register value
    // Format: bits 16-18 specify the format
    uint32_t rt_format = (color_targets[i] >> 16) & 0x7;
    
    // Get render target dimensions from RB_SURFACE_INFO
    auto rb_surface_info = register_file_->values[XE_GPU_REG_RB_SURFACE_INFO];
    uint32_t surface_pitch = rb_surface_info & 0x3FFF;
    uint32_t msaa_samples = (rb_surface_info >> 16) & 0x3;
    
    // Calculate dimensions from pitch and format
    // For now use common resolutions
    RenderTargetDescriptor desc = {};
    desc.base_address = color_targets[i] & 0xFFFFF000;  // Mask off flags
    desc.width = surface_pitch ? surface_pitch : 1280;
    desc.height = 720;  // Common Xbox 360 resolution
    desc.format = rt_format;
    desc.msaa_samples = msaa_samples ? (1 << msaa_samples) : 1;
    desc.is_depth = false;

    // Check cache first
    auto it = render_target_cache_.find(desc);
    if (it != render_target_cache_.end()) {
      current_color_targets_[i] = it->second.get();
      XELOGI("Metal render target cache: BINDING REAL color target {} - texture ptr: 0x{:016x}, size: {}x{}, EDRAM: 0x{:08X}",
             i, reinterpret_cast<uintptr_t>(it->second->texture),
             it->second->width, it->second->height, desc.base_address);
      continue;
    }

    // Create new render target
    MTL::PixelFormat metal_format = ConvertColorFormat(
        static_cast<xenos::ColorRenderTargetFormat>(desc.format));
    if (metal_format == MTL::PixelFormatInvalid) {
      XELOGE("Metal render target cache: Unsupported color format {}", desc.format);
      continue;
    }

    XELOGI("Metal render target cache: Creating color target {} - {}x{}, format {}, {} samples (MSAA: {}), EDRAM address: 0x{:08X}",
           i, desc.width, desc.height, static_cast<uint32_t>(metal_format), desc.msaa_samples,
           desc.msaa_samples > 1 ? "YES" : "NO", desc.base_address);
    
    MTL::Texture* texture = CreateColorTarget(desc.width, desc.height, metal_format, desc.msaa_samples);
    if (!texture) {
      XELOGE("Metal render target cache: Failed to create color target {}", i);
      continue;
    }

    auto metal_rt = std::make_unique<MetalRenderTarget>();
    metal_rt->texture = texture;
    metal_rt->width = desc.width;
    metal_rt->height = desc.height;
    metal_rt->format = metal_format;
    metal_rt->samples = desc.msaa_samples;
    metal_rt->is_depth = false;
    metal_rt->edram_base = desc.base_address;  // Store EDRAM address for later use

    current_color_targets_[i] = metal_rt.get();
    XELOGI("Metal render target cache: CREATED AND BOUND REAL color target {} - texture ptr: 0x{:016x}, size: {}x{}, EDRAM: 0x{:08X}",
           i, reinterpret_cast<uintptr_t>(texture),
           desc.width, desc.height, desc.base_address);
    render_target_cache_[desc] = std::move(metal_rt);
  }

  // Set up depth target
  if (depth_target != 0) {
    // Parse depth format from RB_DEPTH_INFO (bit 16)
    uint32_t depth_format_bit = (depth_target >> 16) & 0x1;
    auto depth_format = static_cast<xenos::DepthRenderTargetFormat>(depth_format_bit);
    
    XELOGI("Metal render target cache: Depth target info: 0x{:08X}, format bit: {}, format: {}", 
           depth_target, depth_format_bit, static_cast<uint32_t>(depth_format));
    
    // Get dimensions and MSAA from RB_SURFACE_INFO (same as color targets)
    auto rb_surface_info = register_file_->values[XE_GPU_REG_RB_SURFACE_INFO];
    uint32_t surface_pitch = rb_surface_info & 0x3FFF;
    uint32_t msaa_samples = (rb_surface_info >> 16) & 0x3;
    
    RenderTargetDescriptor desc = {};
    desc.base_address = depth_target & 0xFFFFF000;  // Mask off flags
    desc.width = surface_pitch ? surface_pitch : 1280;
    desc.height = 720;  // Common Xbox 360 resolution
    desc.format = static_cast<uint32_t>(depth_format);
    desc.msaa_samples = msaa_samples ? (1 << msaa_samples) : 1;
    desc.is_depth = true;

    // Check cache first
    auto it = render_target_cache_.find(desc);
    if (it != render_target_cache_.end()) {
      current_depth_target_ = it->second.get();
    } else {
      // Create new depth target
      MTL::PixelFormat metal_format = ConvertDepthFormat(
          static_cast<xenos::DepthRenderTargetFormat>(desc.format));
      if (metal_format == MTL::PixelFormatInvalid) {
        XELOGE("Metal render target cache: Unsupported depth format {}", desc.format);
      } else {
        XELOGI("Metal render target cache: Creating depth target - {}x{}, format {}, {} samples (MSAA: {}), EDRAM address: 0x{:08X}",
               desc.width, desc.height, static_cast<uint32_t>(metal_format), desc.msaa_samples,
               desc.msaa_samples > 1 ? "YES" : "NO", desc.base_address);
        
        MTL::Texture* texture = CreateDepthTarget(desc.width, desc.height, metal_format, desc.msaa_samples);
        if (texture) {
          auto metal_rt = std::make_unique<MetalRenderTarget>();
          metal_rt->texture = texture;
          metal_rt->width = desc.width;
          metal_rt->height = desc.height;
          metal_rt->format = metal_format;
          metal_rt->samples = desc.msaa_samples;
          metal_rt->is_depth = true;
          metal_rt->edram_base = desc.base_address;  // Store EDRAM address for later use

          current_depth_target_ = metal_rt.get();
          render_target_cache_[desc] = std::move(metal_rt);
        }
      }
    }
  }

  XELOGD("Metal render target cache: Set {} color targets, depth: {}",
         rt_count, depth_target != 0 ? "yes" : "no");

  return true;
}

void MetalRenderTargetCache::LoadRenderTargetsFromEDRAM(MTL::CommandBuffer* command_buffer) {
  SCOPE_profile_cpu_f("gpu");
  
  if (!edram_buffer_) {
    XELOGW("Metal render target cache: LoadRenderTargetsFromEDRAM - No EDRAM buffer");
    return;
  }
  
  XELOGI("Metal render target cache: LoadRenderTargetsFromEDRAM - Loading color targets + depth target");
  
  // Use provided command buffer, or skip if none provided
  MTL::CommandBuffer* cb = command_buffer;
  if (!cb) {
    XELOGW("Metal render target cache: No command buffer provided for EDRAM load, skipping");
    return;
  }
  
  auto blit_encoder = cb->blitCommandEncoder();
  if (!blit_encoder) {
    XELOGW("Metal render target cache: Failed to create blit encoder for EDRAM load");
    return;
  }
  
  // Load color targets
  for (size_t i = 0; i < 4; ++i) {
    auto target = current_color_targets_[i];
    if (target && target->texture) {
      uint32_t edram_offset = target->edram_base;
      uint32_t bytes_per_pixel = 4; // Assume RGBA8 for now
      
      // Use actual texture dimensions, not stored width/height which might be wrong
      uint32_t actual_width = static_cast<uint32_t>(target->texture->width());
      uint32_t actual_height = static_cast<uint32_t>(target->texture->height());
      uint32_t stride = actual_width * bytes_per_pixel;
      uint32_t bytes_needed = stride * actual_height;
      
      XELOGD("Metal render target cache: Loading color target {} from EDRAM offset 0x{:08X} (texture: {}x{}, stored: {}x{}, {} bytes)",
             i, edram_offset, actual_width, actual_height, target->width, target->height, bytes_needed);
      
      // Copy from EDRAM buffer to texture
      blit_encoder->copyFromBuffer(
          edram_buffer_, edram_offset, stride, bytes_needed,
          MTL::Size::Make(actual_width, actual_height, 1),
          target->texture, 0, 0, MTL::Origin::Make(0, 0, 0));
    }
  }
  
  // Load depth target
  if (current_depth_target_ && current_depth_target_->texture) {
    // Skip depth/stencil textures - Metal doesn't allow buffer ↔ depth/stencil texture blits
    XELOGD("Metal render target cache: Skipping depth target load (Metal doesn't support depth↔buffer blits)");
  }
  
  blit_encoder->endEncoding();
  
  XELOGD("Metal render target cache: LoadRenderTargetsFromEDRAM completed");
}

void MetalRenderTargetCache::StoreRenderTargetsToEDRAM(MTL::CommandBuffer* command_buffer) {
  SCOPE_profile_cpu_f("gpu");
  
  if (!edram_buffer_) {
    XELOGW("Metal render target cache: StoreRenderTargetsToEDRAM - No EDRAM buffer");
    return;
  }
  
  XELOGI("Metal render target cache: StoreRenderTargetsToEDRAM - Storing color targets + depth target");
  
  // Use provided command buffer, or skip if none provided
  MTL::CommandBuffer* cb = command_buffer;
  if (!cb) {
    XELOGW("Metal render target cache: No command buffer provided for EDRAM store, skipping");
    return;
  }
  
  auto blit_encoder = cb->blitCommandEncoder();
  if (!blit_encoder) {
    XELOGW("Metal render target cache: Failed to create blit encoder for EDRAM store");
    return;
  }
  
  // Store color targets
  for (size_t i = 0; i < 4; ++i) {
    auto target = current_color_targets_[i];
    if (target && target->texture) {
      uint32_t edram_offset = target->edram_base;
      uint32_t bytes_per_pixel = 4; // Assume RGBA8 for now
      
      // Use actual texture dimensions, not stored width/height which might be wrong
      uint32_t actual_width = static_cast<uint32_t>(target->texture->width());
      uint32_t actual_height = static_cast<uint32_t>(target->texture->height());
      uint32_t stride = actual_width * bytes_per_pixel;
      uint32_t bytes_needed = stride * actual_height;
      
      XELOGD("Metal render target cache: Storing color target {} to EDRAM offset 0x{:08X} (texture: {}x{}, stored: {}x{}, {} bytes)",
             i, edram_offset, actual_width, actual_height, target->width, target->height, bytes_needed);
      
      // Copy from texture to EDRAM buffer
      blit_encoder->copyFromTexture(
          target->texture, 0, 0, MTL::Origin::Make(0, 0, 0),
          MTL::Size::Make(actual_width, actual_height, 1),
          edram_buffer_, edram_offset, stride, bytes_needed);
    }
  }
  
  // Store depth target
  if (current_depth_target_ && current_depth_target_->texture) {
    // Skip depth/stencil textures - Metal doesn't allow buffer ↔ depth/stencil texture blits
    XELOGD("Metal render target cache: Skipping depth target store (Metal doesn't support depth↔buffer blits)");
  }
  
  blit_encoder->endEncoding();
  
  XELOGD("Metal render target cache: StoreRenderTargetsToEDRAM completed");
}

bool MetalRenderTargetCache::Resolve(Memory& memory, uint32_t& written_address, uint32_t& written_length) {
  SCOPE_profile_cpu_f("gpu");

  // Get the resolve region from the registers
  uint32_t rb_copy_control = register_file_->values[XE_GPU_REG_RB_COPY_CONTROL];
  
  // Extract copy command (bits 0-2)
  uint32_t copy_command = rb_copy_control & 0x7;
  
  // Check if this is a resolve operation (value 3 = resolve to texture)
  if (copy_command != 3) {
    // Not a resolve operation, might be raw copy or clear
    XELOGD("Metal render target cache: Non-resolve copy command {}", copy_command);
    written_address = 0;
    written_length = 0;
    return true;
  }

  // Get resolve parameters from registers
  uint32_t rb_copy_dest_info = register_file_->values[XE_GPU_REG_RB_COPY_DEST_INFO];
  uint32_t rb_copy_dest_base = register_file_->values[XE_GPU_REG_RB_COPY_DEST_BASE];
  uint32_t rb_copy_dest_pitch = register_file_->values[XE_GPU_REG_RB_COPY_DEST_PITCH];
  
  // Parse destination info register
  // bits 0-10: width minus 1
  // bits 11-21: height minus 1  
  // bits 22-27: format
  uint32_t dest_width = (rb_copy_dest_info & 0x7FF) + 1;
  uint32_t dest_height = ((rb_copy_dest_info >> 11) & 0x7FF) + 1;
  uint32_t dest_format = (rb_copy_dest_info >> 22) & 0x3F;
  
  // Destination address is in 4KB pages
  uint32_t dest_address = (rb_copy_dest_base & 0xFFFFF) << 12;
  
  // Pitch is in pixels
  uint32_t dest_pitch = rb_copy_dest_pitch & 0x3FFF;
  
  // Get source surface info (which render target to resolve from)
  uint32_t rb_surface_info = register_file_->values[XE_GPU_REG_RB_SURFACE_INFO];
  uint32_t surface_pitch = rb_surface_info & 0x3FFF;
  uint32_t msaa_samples = (rb_surface_info >> 16) & 0x3;
  
  // Calculate destination size (rough estimate - depends on format)
  // For now assume 4 bytes per pixel
  uint32_t bytes_per_pixel = 4;
  uint32_t dest_size = dest_pitch * dest_height * bytes_per_pixel;
  
  // TODO: Implement actual resolve operation
  // This would involve:
  // 1. Reading from the current render target or EDRAM
  // 2. Converting format if needed
  // 3. Resolving MSAA samples if present
  // 4. Writing to guest memory at dest_address
  
  XELOGD("Metal render target cache: Resolve to 0x{:08X}, {}x{}, format {}, pitch {}",
         dest_address, dest_width, dest_height, dest_format, dest_pitch);
  
  // For now, just mark the memory as written
  written_address = dest_address;
  written_length = dest_size;
  
  // Mark the memory range as invalidated (data has been written)
  // Note: InvalidatePhysicalMemory might not exist, just return for now
  // The memory system will handle invalidation when needed
  
  return true;
}

MTL::Texture* MetalRenderTargetCache::GetColorTarget(uint32_t index) const {
  if (index >= 4 || !current_color_targets_[index]) {
    return nullptr;
  }
  return current_color_targets_[index]->texture;
}

MTL::Texture* MetalRenderTargetCache::GetDepthTarget() const {
  if (!current_depth_target_) {
    return nullptr;
  }
  return current_depth_target_->texture;
}

MTL::Texture* MetalRenderTargetCache::GetDummyColorTarget() const {
  if (!dummy_color_target_) {
    return nullptr;
  }
  return dummy_color_target_->texture;
}

void MetalRenderTargetCache::RestoreEdram(const void* snapshot) {
  if (!edram_buffer_ || !snapshot) {
    XELOGE("MetalRenderTargetCache::RestoreEdram: No EDRAM buffer or snapshot");
    return;
  }
  
  // Copy the snapshot to EDRAM buffer
  // The snapshot contains the full EDRAM contents from the trace
  constexpr size_t kEdramSizeBytes = 10 * 1024 * 1024;  // 10MB EDRAM
  memcpy(edram_buffer_->contents(), snapshot, kEdramSizeBytes);
  
  // Mark the buffer as modified so Metal knows to update GPU copy
  edram_buffer_->didModifyRange(NS::Range::Make(0, kEdramSizeBytes));
  
  XELOGI("MetalRenderTargetCache::RestoreEdram: Restored {}MB EDRAM snapshot to buffer", 
         kEdramSizeBytes / (1024 * 1024));
}

MTL::RenderPassDescriptor* MetalRenderTargetCache::GetRenderPassDescriptor(uint32_t expected_sample_count) {
  if (!render_pass_descriptor_dirty_ && cached_render_pass_descriptor_) {
    return cached_render_pass_descriptor_;
  }

  // Release old descriptor safely
  if (cached_render_pass_descriptor_) {
    cached_render_pass_descriptor_->release();
    cached_render_pass_descriptor_ = nullptr;
  }
  
  // Create new descriptor using the convenience constructor
  // This returns an autoreleased object, so we need to retain it
  cached_render_pass_descriptor_ = MTL::RenderPassDescriptor::renderPassDescriptor();
  if (cached_render_pass_descriptor_) {
    cached_render_pass_descriptor_->retain();
  } else {
    return nullptr;
  }

  // Check if we have any render targets
  bool has_attachments = false;
  
  // Set up color attachments
  for (uint32_t i = 0; i < 4; ++i) {
    if (current_color_targets_[i]) {
      MTL::RenderPassColorAttachmentDescriptor* color_attachment = 
          cached_render_pass_descriptor_->colorAttachments()->object(i);
      
      color_attachment->setTexture(current_color_targets_[i]->texture);
      
      // Clear on first use, then preserve contents between passes
      if (current_color_targets_[i]->needs_clear) {
        color_attachment->setLoadAction(MTL::LoadActionClear);
        color_attachment->setClearColor(MTL::ClearColor(0.0, 0.0, 0.0, 1.0));  // Clear to black
        current_color_targets_[i]->needs_clear = false;
      } else {
        color_attachment->setLoadAction(MTL::LoadActionLoad);
      }
      color_attachment->setStoreAction(MTL::StoreActionStore);
      has_attachments = true;
    }
  }

  // Set up depth attachment
  if (current_depth_target_) {
    MTL::RenderPassDepthAttachmentDescriptor* depth_attachment = 
        cached_render_pass_descriptor_->depthAttachment();
    
    depth_attachment->setTexture(current_depth_target_->texture);
    
    // Clear on first use, then preserve contents between passes
    if (current_depth_target_->needs_clear) {
      depth_attachment->setLoadAction(MTL::LoadActionClear);
      depth_attachment->setClearDepth(1.0);
      current_depth_target_->needs_clear = false;
    } else {
      depth_attachment->setLoadAction(MTL::LoadActionLoad);
    }
    depth_attachment->setStoreAction(MTL::StoreActionStore);
    has_attachments = true;
    
    // IMPORTANT: If we have a depth attachment but no color attachments,
    // we still need a dummy color attachment because the fragment shader
    // will likely write color output. Metal throws an exception if the
    // fragment shader writes color but there's no color attachment.
    bool has_color_attachment = false;
    for (uint32_t i = 0; i < 4; ++i) {
      if (current_color_targets_[i]) {
        has_color_attachment = true;
        break;
      }
    }
    
    if (!has_color_attachment) {
      XELOGW("Metal render target cache: Depth-only render pass - adding dummy color target for shader compatibility");
      // CRITICAL: Use the expected sample count from pipeline to avoid Metal validation errors!
      // The pipeline and render target MUST have matching sample counts
      uint32_t sample_count = expected_sample_count ? expected_sample_count : 1;
      
      // CRITICAL: Dummy target MUST match depth target dimensions to avoid scissor rect validation errors!
      // Use the depth target's dimensions if available, otherwise use default Xbox 360 resolution
      uint32_t width = current_depth_target_ ? current_depth_target_->width : 1280;
      uint32_t height = current_depth_target_ ? current_depth_target_->height : 720;
      
      // Create dummy texture if it doesn't exist or properties changed
      // Must recreate if sample count or dimensions change to match pipeline/depth target
      if (!dummy_color_target_ || 
          dummy_color_target_->samples != sample_count ||
          dummy_color_target_->width != width ||
          dummy_color_target_->height != height) {
        MTL::Texture* dummy_texture = CreateColorTarget(
            width, height, MTL::PixelFormatBGRA8Unorm, sample_count);
        if (dummy_texture) {
          dummy_color_target_ = std::make_unique<MetalRenderTarget>();
          dummy_color_target_->texture = dummy_texture;
          dummy_color_target_->width = width;
          dummy_color_target_->height = height;
          dummy_color_target_->format = MTL::PixelFormatBGRA8Unorm;
          dummy_color_target_->samples = sample_count;
          dummy_color_target_->is_depth = false;
          dummy_color_target_->edram_base = 0;  // Dummy target not in EDRAM
          
          XELOGI("Metal RenderTargetCache: Created dummy color target {}x{} - texture ptr: 0x{:016x}",
                 width, height, reinterpret_cast<uintptr_t>(dummy_texture));
        }
      }
      
      if (dummy_color_target_) {
        MTL::RenderPassColorAttachmentDescriptor* color_attachment = 
            cached_render_pass_descriptor_->colorAttachments()->object(0);
        
        color_attachment->setTexture(dummy_color_target_->texture);
        color_attachment->setLoadAction(MTL::LoadActionClear);
        color_attachment->setStoreAction(MTL::StoreActionStore);  // Store contents for capture
        // Use BLACK clear to see what the draw calls produce
      color_attachment->setClearColor(MTL::ClearColor(0.0, 0.0, 0.0, 1.0));  // Black
        
        XELOGI("Metal RenderTargetCache: Set dummy color target in render pass - texture ptr: 0x{:016x}",
               reinterpret_cast<uintptr_t>(dummy_color_target_->texture));
      }
    }
  }
  
  // If no render targets are set at all, create a dummy render target
  // This is needed because Metal requires at least one attachment
  if (!has_attachments) {
    XELOGW("Metal render target cache: No render targets set, creating dummy target with {} samples", 
           expected_sample_count);
    
    // CRITICAL: Use the expected sample count from pipeline to avoid Metal validation errors!
    uint32_t sample_count = expected_sample_count ? expected_sample_count : 1;
    
    // Get dimensions from RB_SURFACE_INFO register to match what the game expects
    auto rb_surface_info = register_file_->values[XE_GPU_REG_RB_SURFACE_INFO];
    uint32_t surface_pitch = rb_surface_info & 0x3FFF;
    uint32_t width = surface_pitch ? surface_pitch : 1280;
    uint32_t height = 720;  // Common Xbox 360 resolution
    
    // Create dummy texture if it doesn't exist or properties changed
    // Must recreate if sample count or dimensions change to match pipeline requirements
    if (!dummy_color_target_ || 
        dummy_color_target_->samples != sample_count ||
        dummy_color_target_->width != width ||
        dummy_color_target_->height != height) {
      MTL::Texture* dummy_texture = CreateColorTarget(
          width, height, MTL::PixelFormatBGRA8Unorm, sample_count);
      if (dummy_texture) {
        dummy_color_target_ = std::make_unique<MetalRenderTarget>();
        dummy_color_target_->texture = dummy_texture;
        dummy_color_target_->width = width;
        dummy_color_target_->height = height;
        dummy_color_target_->format = MTL::PixelFormatBGRA8Unorm;
        dummy_color_target_->samples = sample_count;
        dummy_color_target_->is_depth = false;
        dummy_color_target_->edram_base = 0;  // Dummy target not in EDRAM
      }
    }
    
    if (dummy_color_target_) {
      XELOGI("Metal render target cache: NO REAL TARGETS - Using DUMMY target, texture ptr: 0x{:016x}, size: {}x{}",
             reinterpret_cast<uintptr_t>(dummy_color_target_->texture),
             dummy_color_target_->width, dummy_color_target_->height);
      
      MTL::RenderPassColorAttachmentDescriptor* color_attachment = 
          cached_render_pass_descriptor_->colorAttachments()->object(0);
      
      color_attachment->setTexture(dummy_color_target_->texture);
      
      // Clear only on first use of the frame, then preserve contents
      if (dummy_color_target_needs_clear_) {
        color_attachment->setLoadAction(MTL::LoadActionClear);
        color_attachment->setClearColor(MTL::ClearColor(0.0, 0.0, 0.0, 1.0));  // Black
        dummy_color_target_needs_clear_ = false;
      } else {
        // Preserve previous render pass contents to accumulate all draws
        color_attachment->setLoadAction(MTL::LoadActionLoad);
      }
      color_attachment->setStoreAction(MTL::StoreActionStore);  // Store contents for capture
    }
  }

  render_pass_descriptor_dirty_ = false;
  return cached_render_pass_descriptor_;
}

MTL::Texture* MetalRenderTargetCache::CreateColorTarget(uint32_t width, uint32_t height,
                                                        MTL::PixelFormat format, uint32_t samples) {
  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    XELOGE("Metal render target cache: Failed to get Metal device from command processor");
    return nullptr;
  }

  MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
  
  // Set texture type based on MSAA
  if (samples > 1) {
    descriptor->setTextureType(MTL::TextureType2DMultisample);
    descriptor->setSampleCount(samples);
  } else {
    descriptor->setTextureType(MTL::TextureType2D);
  }
  
  descriptor->setPixelFormat(format);
  descriptor->setWidth(width);
  descriptor->setHeight(height);
  descriptor->setMipmapLevelCount(1);
  descriptor->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  descriptor->setStorageMode(MTL::StorageModePrivate);

  MTL::Texture* texture = device->newTexture(descriptor);
  
  descriptor->release();
  device->release();

  if (!texture) {
    XELOGE("Metal render target cache: Failed to create color target {}x{}", width, height);
    return nullptr;
  }

  return texture;
}

MTL::Texture* MetalRenderTargetCache::CreateDepthTarget(uint32_t width, uint32_t height,
                                                        MTL::PixelFormat format, uint32_t samples) {
  MTL::Device* device = command_processor_->GetMetalDevice();
  if (!device) {
    XELOGE("Metal render target cache: Failed to get Metal device from command processor");
    return nullptr;
  }

  MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
  
  // Set texture type based on MSAA
  if (samples > 1) {
    descriptor->setTextureType(MTL::TextureType2DMultisample);
    descriptor->setSampleCount(samples);
  } else {
    descriptor->setTextureType(MTL::TextureType2D);
  }
  
  descriptor->setPixelFormat(format);
  descriptor->setWidth(width);
  descriptor->setHeight(height);
  descriptor->setMipmapLevelCount(1);
  descriptor->setUsage(MTL::TextureUsageRenderTarget);
  descriptor->setStorageMode(MTL::StorageModePrivate);

  MTL::Texture* texture = device->newTexture(descriptor);
  
  descriptor->release();

  if (!texture) {
    XELOGE("Metal render target cache: Failed to create depth target {}x{}", width, height);
    return nullptr;
  }

  return texture;
}

MTL::PixelFormat MetalRenderTargetCache::ConvertColorFormat(xenos::ColorRenderTargetFormat format) {
  switch (format) {
    case xenos::ColorRenderTargetFormat::k_8_8_8_8:
      return MTL::PixelFormatRGBA8Unorm;
    case xenos::ColorRenderTargetFormat::k_8_8_8_8_GAMMA:
      return MTL::PixelFormatRGBA8Unorm_sRGB;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10:
      return MTL::PixelFormatRGB10A2Unorm;
    case xenos::ColorRenderTargetFormat::k_2_10_10_10_FLOAT:
      return MTL::PixelFormatRG11B10Float;
    case xenos::ColorRenderTargetFormat::k_16_16:
      return MTL::PixelFormatRG16Unorm;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16:
      return MTL::PixelFormatRGBA16Unorm;
    case xenos::ColorRenderTargetFormat::k_16_16_FLOAT:
      return MTL::PixelFormatRG16Float;
    case xenos::ColorRenderTargetFormat::k_16_16_16_16_FLOAT:
      return MTL::PixelFormatRGBA16Float;
    case xenos::ColorRenderTargetFormat::k_32_FLOAT:
      return MTL::PixelFormatR32Float;
    case xenos::ColorRenderTargetFormat::k_32_32_FLOAT:
      return MTL::PixelFormatRG32Float;
    default:
      XELOGW("Metal render target cache: Unsupported color format {}", 
             static_cast<uint32_t>(format));
      return MTL::PixelFormatInvalid;
  }
}

MTL::PixelFormat MetalRenderTargetCache::ConvertDepthFormat(xenos::DepthRenderTargetFormat format) {
  switch (format) {
    case xenos::DepthRenderTargetFormat::kD24S8:
      // Depth24Unorm_Stencil8 is not supported on macOS, use Depth32Float_Stencil8 instead
      // This provides more precision than needed but is the closest available format
      return MTL::PixelFormatDepth32Float_Stencil8;
    case xenos::DepthRenderTargetFormat::kD24FS8:
      return MTL::PixelFormatDepth32Float_Stencil8;
    default:
      XELOGW("Metal render target cache: Unsupported depth format {}", 
             static_cast<uint32_t>(format));
      return MTL::PixelFormatInvalid;
  }
}

bool MetalRenderTargetCache::RenderTargetDescriptor::operator==(const RenderTargetDescriptor& other) const {
  return base_address == other.base_address &&
         width == other.width &&
         height == other.height &&
         format == other.format &&
         msaa_samples == other.msaa_samples &&
         is_depth == other.is_depth;
}

size_t MetalRenderTargetCache::RenderTargetDescriptorHasher::operator()(const RenderTargetDescriptor& desc) const {
  size_t hash = 0;
  hash ^= std::hash<uint32_t>{}(desc.base_address) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint32_t>{}(desc.width) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint32_t>{}(desc.height) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<uint32_t>{}(desc.format) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  hash ^= std::hash<bool>{}(desc.is_depth) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  return hash;
}

}  // namespace metal
}  // namespace gpu
}  // namespace xe
