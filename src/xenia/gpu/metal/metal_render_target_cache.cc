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

namespace xe {
namespace gpu {
namespace metal {

MetalRenderTargetCache::MetalRenderTargetCache(MetalCommandProcessor* command_processor,
                                               const RegisterFile* register_file,
                                               Memory* memory)
    : command_processor_(command_processor),
      register_file_(register_file),
      memory_(memory) {
#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  cached_render_pass_descriptor_ = nullptr;
  render_pass_descriptor_dirty_ = true;
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE
}

MetalRenderTargetCache::~MetalRenderTargetCache() {
  Shutdown();
}

bool MetalRenderTargetCache::Initialize() {
  SCOPE_profile_cpu_f("gpu");

  XELOGD("Metal render target cache: Initialized successfully");
  return true;
}

void MetalRenderTargetCache::Shutdown() {
  SCOPE_profile_cpu_f("gpu");

  ClearCache();

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  if (cached_render_pass_descriptor_) {
    cached_render_pass_descriptor_->release();
    cached_render_pass_descriptor_ = nullptr;
  }
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

  XELOGD("Metal render target cache: Shutdown complete");
}

void MetalRenderTargetCache::ClearCache() {
  SCOPE_profile_cpu_f("gpu");

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  // Clear current render targets
  for (auto& rt : current_color_targets_) {
    rt.reset();
  }
  current_depth_target_.reset();

  // Clear cache
  render_target_cache_.clear();
  
  render_pass_descriptor_dirty_ = true;
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

  XELOGD("Metal render target cache: Cache cleared");
}

bool MetalRenderTargetCache::SetRenderTargets(uint32_t rt_count, const uint32_t* color_targets,
                                              uint32_t depth_target) {
  SCOPE_profile_cpu_f("gpu");

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)
  render_pass_descriptor_dirty_ = true;

  // Clear existing targets
  for (auto& rt : current_color_targets_) {
    rt.reset();
  }
  current_depth_target_.reset();

  // Set up color targets
  for (uint32_t i = 0; i < rt_count && i < 4; ++i) {
    if (color_targets[i] == 0) {
      continue;
    }

    // TODO: Parse Xbox 360 render target configuration from registers
    // For now, create basic targets
    RenderTargetDescriptor desc = {};
    desc.base_address = color_targets[i];
    desc.width = 1280;  // TODO: Get from registers
    desc.height = 720;  // TODO: Get from registers
    desc.format = static_cast<uint32_t>(xenos::ColorRenderTargetFormat::k_8_8_8_8); // TODO: Get from registers
    desc.msaa_samples = 1;  // TODO: Get from registers
    desc.is_depth = false;

    // Check cache first
    auto it = render_target_cache_.find(desc);
    if (it != render_target_cache_.end()) {
      current_color_targets_[i] = std::make_unique<MetalRenderTarget>(*it->second);
      continue;
    }

    // Create new render target
    MTL::PixelFormat metal_format = ConvertColorFormat(
        static_cast<xenos::ColorRenderTargetFormat>(desc.format));
    if (metal_format == MTL::PixelFormatInvalid) {
      XELOGE("Metal render target cache: Unsupported color format {}", desc.format);
      continue;
    }

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

    current_color_targets_[i] = std::make_unique<MetalRenderTarget>(*metal_rt);
    render_target_cache_[desc] = std::move(metal_rt);
  }

  // Set up depth target
  if (depth_target != 0) {
    RenderTargetDescriptor desc = {};
    desc.base_address = depth_target;
    desc.width = 1280;  // TODO: Get from registers
    desc.height = 720;  // TODO: Get from registers
    desc.format = static_cast<uint32_t>(xenos::DepthRenderTargetFormat::kD24S8); // TODO: Get from registers
    desc.msaa_samples = 1;  // TODO: Get from registers
    desc.is_depth = true;

    // Check cache first
    auto it = render_target_cache_.find(desc);
    if (it != render_target_cache_.end()) {
      current_depth_target_ = std::make_unique<MetalRenderTarget>(*it->second);
    } else {
      // Create new depth target
      MTL::PixelFormat metal_format = ConvertDepthFormat(
          static_cast<xenos::DepthRenderTargetFormat>(desc.format));
      if (metal_format == MTL::PixelFormatInvalid) {
        XELOGE("Metal render target cache: Unsupported depth format {}", desc.format);
      } else {
        MTL::Texture* texture = CreateDepthTarget(desc.width, desc.height, metal_format, desc.msaa_samples);
        if (texture) {
          auto metal_rt = std::make_unique<MetalRenderTarget>();
          metal_rt->texture = texture;
          metal_rt->width = desc.width;
          metal_rt->height = desc.height;
          metal_rt->format = metal_format;
          metal_rt->samples = desc.msaa_samples;
          metal_rt->is_depth = true;

          current_depth_target_ = std::make_unique<MetalRenderTarget>(*metal_rt);
          render_target_cache_[desc] = std::move(metal_rt);
        }
      }
    }
  }

  XELOGD("Metal render target cache: Set {} color targets, depth: {}",
         rt_count, depth_target != 0 ? "yes" : "no");
#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

  return true;
}

#if XE_PLATFORM_MAC && defined(METAL_CPP_AVAILABLE)

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

MTL::RenderPassDescriptor* MetalRenderTargetCache::GetRenderPassDescriptor() const {
  if (!render_pass_descriptor_dirty_ && cached_render_pass_descriptor_) {
    return cached_render_pass_descriptor_;
  }

  // Release old descriptor
  if (cached_render_pass_descriptor_) {
    cached_render_pass_descriptor_->release();
    cached_render_pass_descriptor_ = nullptr;
  }

  // Create new descriptor
  cached_render_pass_descriptor_ = MTL::RenderPassDescriptor::alloc()->init();
  if (!cached_render_pass_descriptor_) {
    return nullptr;
  }

  // Set up color attachments
  for (uint32_t i = 0; i < 4; ++i) {
    if (current_color_targets_[i]) {
      MTL::RenderPassColorAttachmentDescriptor* color_attachment = 
          cached_render_pass_descriptor_->colorAttachments()->object(i);
      
      color_attachment->setTexture(current_color_targets_[i]->texture);
      color_attachment->setLoadAction(MTL::LoadActionClear);
      color_attachment->setStoreAction(MTL::StoreActionStore);
      color_attachment->setClearColor(MTL::ClearColor(0.0, 0.0, 0.0, 1.0));
    }
  }

  // Set up depth attachment
  if (current_depth_target_) {
    MTL::RenderPassDepthAttachmentDescriptor* depth_attachment = 
        cached_render_pass_descriptor_->depthAttachment();
    
    depth_attachment->setTexture(current_depth_target_->texture);
    depth_attachment->setLoadAction(MTL::LoadActionClear);
    depth_attachment->setStoreAction(MTL::StoreActionStore);
    depth_attachment->setClearDepth(1.0);
  }

  render_pass_descriptor_dirty_ = false;
  return cached_render_pass_descriptor_;
}

MTL::Texture* MetalRenderTargetCache::CreateColorTarget(uint32_t width, uint32_t height,
                                                        MTL::PixelFormat format, uint32_t samples) {
  // TODO: Get Metal device from command processor
  // MTL::Device* device = command_processor_->GetMetalDevice();
  // For now, create a temporary device
  MTL::Device* device = MTL::CreateSystemDefaultDevice();
  if (!device) {
    XELOGE("Metal render target cache: Failed to get Metal device");
    return nullptr;
  }

  MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
  descriptor->setTextureType(MTL::TextureType2D);
  descriptor->setPixelFormat(format);
  descriptor->setWidth(width);
  descriptor->setHeight(height);
  descriptor->setMipmapLevelCount(1);
  descriptor->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  descriptor->setStorageMode(MTL::StorageModePrivate);

  if (samples > 1) {
    descriptor->setSampleCount(samples);
  }

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
  // TODO: Get Metal device from command processor
  // MTL::Device* device = command_processor_->GetMetalDevice();
  // For now, create a temporary device
  MTL::Device* device = MTL::CreateSystemDefaultDevice();
  if (!device) {
    XELOGE("Metal render target cache: Failed to get Metal device");
    return nullptr;
  }

  MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
  descriptor->setTextureType(MTL::TextureType2D);
  descriptor->setPixelFormat(format);
  descriptor->setWidth(width);
  descriptor->setHeight(height);
  descriptor->setMipmapLevelCount(1);
  descriptor->setUsage(MTL::TextureUsageRenderTarget);
  descriptor->setStorageMode(MTL::StorageModePrivate);

  if (samples > 1) {
    descriptor->setSampleCount(samples);
  }

  MTL::Texture* texture = device->newTexture(descriptor);
  
  descriptor->release();
  device->release();

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
      return MTL::PixelFormatDepth24Unorm_Stencil8;
    case xenos::DepthRenderTargetFormat::kD24FS8:
      return MTL::PixelFormatDepth32Float_Stencil8;
    default:
      XELOGW("Metal render target cache: Unsupported depth format {}", 
             static_cast<uint32_t>(format));
      return MTL::PixelFormatInvalid;
  }
}

#endif  // XE_PLATFORM_MAC && METAL_CPP_AVAILABLE

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
